#include "src/recommendation_engine/retrieval/retrievers/user_cf/retriever_user_cf.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "src/recommendation_engine/retrieval/retrievers/user_cf/local_file_user_similarity_store.h"
#include "src/recommendation_engine/retrieval/retrievers/user_cf/redis_user_similarity_store.h"
#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/redis_client/redis_client.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star::recommendation_engine {
namespace {

constexpr char kRetrieverName[] = "user_cf";

using ::grpc::CreateChannel;
using ::grpc::InsecureChannelCredentials;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::RedisClient;
using ::shooting_star::utilities::GenerateGuid;
using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::format;
using ::std::invalid_argument;
using ::std::make_shared;
using ::std::make_unique;
using ::std::runtime_error;
using ::std::shared_ptr;
using ::std::size_t;
using ::std::sort;
using ::std::string;
using ::std::to_string;
using ::std::unique_ptr;
using ::std::unordered_map;
using ::std::unordered_set;
using ::std::vector;

using TriggerSeed = RetrieverBase::TriggerSeed;
using ScoredItem = RetrieverBase::ScoredItem;
using CandidateScore = RetrieverBase::CandidateScore;

void AppendNeighborCandidateItems(
    const ::google::protobuf::RepeatedPtrField<WeightedItem>& items,
    double base_score,
    unordered_set<uint64_t>* seen_candidate_items,
    vector<ScoredItem>* candidate_items) {
  // Convert a neighbor profile channel into ranked weighted candidate items.
  int rank = 0;
  for (const WeightedItem& item : items) {
    ++rank;
    if (item.item_id() <= 0) {
      continue;
    }

    const uint64_t item_id = static_cast<uint64_t>(item.item_id());
    if (!seen_candidate_items->insert(item_id).second) {
      continue;
    }

    const double item_weight = item.weight() > 0.0 ? item.weight() : 1.0;
    candidate_items->emplace_back(ScoredItem{
        .item_id = item_id,
        .score = base_score * item_weight / static_cast<double>(rank),
    });
  }
}

void AddCandidateContribution(
    const TriggerSeed& trigger_seed,
    const ScoredItem& neighbor_item,
    unordered_map<uint64_t, CandidateScore>* candidates) {
  // Accumulate contribution and keep the strongest explanation edge.
  if (neighbor_item.item_id == 0 || neighbor_item.score <= 0.0) {
    return;
  }

  const double contribution = trigger_seed.score * neighbor_item.score;
  if (contribution <= 0.0) {
    return;
  }

  auto [iter, inserted] = candidates->try_emplace(neighbor_item.item_id);
  CandidateScore& candidate = iter->second;
  if (inserted) {
    candidate.item_id = neighbor_item.item_id;
  }
  candidate.score += contribution;

  if (inserted || contribution > candidate.reason_score) {
    candidate.trigger_entity_id = trigger_seed.entity_id;
    candidate.trigger_score = trigger_seed.score;
    candidate.source_score = neighbor_item.score;
    candidate.reason_score = contribution;
  }
}

vector<ScoredItem> CollectNeighborCandidateItems(
    const UserCfProfile& profile) {
  // Merge multiple behavior channels from one similar user's profile.
  unordered_set<uint64_t> seen_candidate_items;
  vector<ScoredItem> candidate_items;

  AppendNeighborCandidateItems(profile.recent_liked_items(),
                               1.0,
                               &seen_candidate_items,
                               &candidate_items);
  AppendNeighborCandidateItems(profile.liked_items(),
                               0.7,
                               &seen_candidate_items,
                               &candidate_items);
  AppendNeighborCandidateItems(profile.interested_items(),
                               0.3,
                               &seen_candidate_items,
                               &candidate_items);

  return candidate_items;
}

vector<CandidateScore> SortCandidates(
    const unordered_map<uint64_t, CandidateScore>& candidates) {
  // Produce deterministic ranking: higher score first, then smaller item id.
  vector<CandidateScore> sorted_candidates;
  sorted_candidates.reserve(candidates.size());
  for (const auto& [_, candidate] : candidates) {
    sorted_candidates.emplace_back(candidate);
  }

  sort(sorted_candidates.begin(), sorted_candidates.end(),
       [](const CandidateScore& lhs, const CandidateScore& rhs) {
         if (lhs.score != rhs.score) {
           return lhs.score > rhs.score;
         }
         return lhs.item_id < rhs.item_id;
       });
  return sorted_candidates;
}

unique_ptr<UserSimilarityStore> CreateUserSimilarityStore(
    const GlobalConfig& config) {
  const string store_type = config.GetSimilarityStoreType();
  if (store_type == UserSimilarityStore::kRedisStoreType) {
    return make_unique<RedisUserSimilarityStore>(
        RedisClient::Create(), config.GetRedisKeyPrefix());
  }
  if (store_type == UserSimilarityStore::kLocalStoreType) {
    const string data_path = ResolveWorkspaceRelativePath(
        config.GetSimilarityDataPath());
    if (data_path.empty()) {
      throw invalid_argument(
          "similarity_store.data_path must not be empty for local user_cf store");
    }
    return make_unique<LocalFileUserSimilarityStore>(data_path);
  }

  throw invalid_argument(format("Unsupported user similarity store type: {}",
                                store_type));
}

}  // namespace

RetrieverUserCf::RetrieverUserCf(
    unique_ptr<UserSimilarityStore> user_similarity_store,
    unique_ptr<ProfileService::StubInterface> profile_stub,
    Options options)
    : RetrieverBase(options.default_max_candidate_count),
      user_similarity_store_(::std::move(user_similarity_store)),
      profile_stub_(::std::move(profile_stub)),
      trigger_seed_user_count_(options.trigger_seed_user_count),
      score_multiplier_(options.score_multiplier) {
  if (user_similarity_store_ == nullptr) {
    throw runtime_error("RetrieverUserCf user_similarity_store must not be null");
  }
  if (profile_stub_ == nullptr) {
    throw runtime_error("RetrieverUserCf profile_stub must not be null");
  }
  if (options.default_max_candidate_count <= 0) {
    throw invalid_argument(
        "RetrieverUserCf default_max_candidate_count must be positive");
  }
  if (trigger_seed_user_count_ <= 0) {
    throw invalid_argument(
        "RetrieverUserCf trigger_seed_user_count must be positive");
  }
  if (score_multiplier_ <= 0.0) {
    throw invalid_argument("RetrieverUserCf score_multiplier must be positive");
  }
}

unique_ptr<RetrieverUserCf> RetrieverUserCf::Create(
    const GlobalConfig& config) {
  Options options;
  options.trigger_seed_user_count = config.GetUserCfTriggerSeedUserCount();
  options.score_multiplier = config.GetRetrieverUserCfScoreMultiplier();

  unique_ptr<UserSimilarityStore> user_similarity_store =
      CreateUserSimilarityStore(config);
  shared_ptr<::grpc::Channel> profile_channel =
      CreateChannel(config.GetProfileServiceAddress(),
                    InsecureChannelCredentials());
  unique_ptr<ProfileService::StubInterface> profile_stub =
      ProfileService::NewStub(::std::move(profile_channel));

  unique_ptr<RetrieverUserCf> server = make_unique<RetrieverUserCf>(
      ::std::move(user_similarity_store),
      ::std::move(profile_stub),
      options);
  return server;
}

Status RetrieverUserCf::DoRetrieve(const RetrieverRequest& request,
                                   RetrieverResponse* response) const {
  // Initialize request-scoped context and log the retrieval start.
  const auto session = make_shared<SessionData>();
  session->trace_id = request.trace_id();

  const auto& logger = LoggerRegistry::Get();
  logger.Debug(
      "user_cf_retrieve_started",
      {
          {"trace_id", request.trace_id()},
          {"user_id", to_string(request.user_id())},
          {"max_candidate_count", to_string(request.max_candidate_count())},
      });

  // Step 1: Load similar users as trigger seeds.
  const Status trigger_seed_status =
      LoadTriggerSeeds(request, session, response);
  if (!trigger_seed_status.ok() || session->trigger_seeds.empty()) {
    return trigger_seed_status;
  }

  // Step 2: Build requester item exclusion set.
  BuildFilterSet(request.profile(), session);

  // Step 3: Load profiles for trigger users.
  const Status profile_status =
      LoadTriggerUserProfiles(request, session, response);
  if (!profile_status.ok()) {
    return profile_status;
  }

  // Step 4: Aggregate candidate contributions from similar users.
  AggregateCandidates(session);
  logger.Debug(
      "user_cf_profile_aggregation_done",
      {
          {"trace_id", request.trace_id()},
          {"user_id", to_string(request.user_id())},
          {"missing_profile_count", to_string(session->missing_profile_count)},
          {"total_neighbor_items_seen",
           to_string(session->total_neighbor_items_seen)},
          {"total_neighbor_items_filtered_out",
           to_string(session->total_neighbor_items_filtered_out)},
          {"candidate_pool_size", to_string(session->candidates.size())},
      });

  // Step 5: Rank and write the final candidate list into the response.
  FillResponseCandidates(session, request, response);

  // Mark success and emit completion logs.
  response->set_status(RetrieverServiceStatus::RETRIEVER_SUCCESS);
  logger.Debug(
      "user_cf_retrieve_finished",
      {
          {"user_id", to_string(request.user_id())},
          {"candidate_count", to_string(response->candidates_size())},
      });
  return Status::OK;
}

Status RetrieverUserCf::LoadTriggerSeeds(
    const RetrieverRequest& request,
    const shared_ptr<SessionData>& session,
    RetrieverResponse* response) const {
  const auto& logger = LoggerRegistry::Get();

  try {
    // Query top-N similar users and convert them into trigger seeds.
    const uint64_t request_user_id = static_cast<uint64_t>(request.user_id());
    const vector<UserNeighbor> user_neighbors =
        user_similarity_store_->FindNeighborsByUserId(request_user_id,
                                                      trigger_seed_user_count_);
    session->trigger_seeds = CollectTriggerSeeds(request_user_id, user_neighbors);
  } catch (const ::std::exception& ex) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_SYSTEM_ERROR);
    logger.Error(
        "user_cf_similarity_lookup_failed",
        {
            {"user_id", to_string(request.user_id())},
            {"error", ex.what()},
        });
    return Status(StatusCode::INTERNAL, ex.what());
  }

  logger.Debug(
      "user_cf_trigger_seeds_ready",
      {
          {"user_id", to_string(request.user_id())},
          {"trigger_seed_count", to_string(session->trigger_seeds.size())},
          {"trigger_seed_user_count",
           to_string(trigger_seed_user_count_)},
      });

  if (session->trigger_seeds.empty()) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_EMPTY_TRIGGER_SEEDS);
    logger.Debug(
        "user_cf_retrieve_early_return_empty_trigger_seeds",
        {
            {"user_id", to_string(request.user_id())},
        });
  }
  return Status::OK;
}

void RetrieverUserCf::BuildFilterSet(
    const Profile& profile,
    const shared_ptr<SessionData>& session) const {
  session->item_ids_to_filter_out = CollectItemIdsToFilterOut(profile);
}

Status RetrieverUserCf::LoadTriggerUserProfiles(
    const RetrieverRequest& request,
    const shared_ptr<SessionData>& session,
    RetrieverResponse* response) const {
  const auto& logger = LoggerRegistry::Get();

  // Build batch request user ids from trigger seeds.
  session->trigger_user_ids.clear();
  session->trigger_user_ids.reserve(session->trigger_seeds.size());
  for (const TriggerSeed& trigger_seed : session->trigger_seeds) {
    session->trigger_user_ids.emplace_back(trigger_seed.entity_id);
  }

  try {
    session->trigger_user_profiles.assign(session->trigger_user_ids.size(),
                                          ::std::nullopt);
    BatchGetUserCfProfilesRequest profile_request;
    profile_request.set_trace_id(request.trace_id());
    profile_request.set_request_id(GenerateGuid());

    vector<size_t> valid_indexes;
    valid_indexes.reserve(session->trigger_user_ids.size());
    for (size_t i = 0; i < session->trigger_user_ids.size(); ++i) {
      const uint64_t user_id = session->trigger_user_ids[i];
      if (user_id == 0 ||
          user_id > static_cast<uint64_t>(::std::numeric_limits<int64_t>::max())) {
        continue;
      }
      profile_request.add_user_ids(static_cast<int64_t>(user_id));
      valid_indexes.emplace_back(i);
    }

    if (!valid_indexes.empty()) {
      BatchGetUserCfProfilesResponse profile_response;
      ::grpc::ClientContext context;
      const Status status = profile_stub_->BatchGetUserCfProfiles(
          &context, profile_request, &profile_response);
      if (!status.ok()) {
        throw runtime_error(format("Failed to fetch user_cf profiles: {}",
                                   status.error_message()));
      }
      if (profile_response.status() !=
          ProfileServiceStatus::PROFILE_SUCCESS) {
        throw runtime_error(
            format("Profile service returned status {} for user_cf profile batch.",
                   static_cast<int>(profile_response.status())));
      }
      if (profile_response.results_size() !=
          static_cast<int>(valid_indexes.size())) {
        throw runtime_error(
            "Profile service user_cf batch returned mismatched result size");
      }

      for (int i = 0; i < profile_response.results_size(); ++i) {
        const UserCfProfileResult& result = profile_response.results(i);
        if (result.status() != ProfileServiceStatus::PROFILE_SUCCESS) {
          continue;
        }
        session->trigger_user_profiles[valid_indexes[static_cast<size_t>(i)]] =
            result.profile();
      }
    }
  } catch (const ::std::exception& ex) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_SYSTEM_ERROR);
    logger.Error(
        "user_cf_profile_lookup_failed",
        {
            {"user_id", to_string(request.user_id())},
            {"error", ex.what()},
        });
    return Status(StatusCode::INTERNAL, ex.what());
  }

  return Status::OK;
}

void RetrieverUserCf::AggregateCandidates(
    const shared_ptr<SessionData>& session) const {
  // Reset per-request aggregation counters and candidate pool.
  session->missing_profile_count = 0;
  session->total_neighbor_items_seen = 0;
  session->total_neighbor_items_filtered_out = 0;
  session->candidates.clear();

  // Merge each valid trigger user's items into the global candidate map.
  for (size_t i = 0; i < session->trigger_user_profiles.size(); ++i) {
    if (!session->trigger_user_profiles[i].has_value()) {
      ++session->missing_profile_count;
      continue;
    }

    const vector<ScoredItem> neighbor_items =
        CollectNeighborCandidateItems(*session->trigger_user_profiles[i]);
    session->total_neighbor_items_seen += neighbor_items.size();
    for (const ScoredItem& neighbor_item : neighbor_items) {
      if (session->item_ids_to_filter_out.contains(neighbor_item.item_id)) {
        ++session->total_neighbor_items_filtered_out;
        continue;
      }
      AddCandidateContribution(session->trigger_seeds[i], neighbor_item,
                               &session->candidates);
    }
  }
}

void RetrieverUserCf::FillResponseCandidates(
    const shared_ptr<SessionData>& session,
    const RetrieverRequest& request,
    RetrieverResponse* response) const {
  // Convert aggregated map to an ordered top-K response.
  const vector<CandidateScore> sorted_candidates =
      SortCandidates(session->candidates);
  for (const CandidateScore& scored_candidate : sorted_candidates) {
    if (response->candidates_size() >= request.max_candidate_count()) {
      break;
    }

    CandidateItem* candidate = response->add_candidates();
    candidate->set_item_id(scored_candidate.item_id);
    candidate->set_item_type(ItemType::ITEM_TYPE_UNSPECIFIED);
    candidate->set_retriever(kRetrieverName);
    candidate->set_retrieve_score(scored_candidate.score * score_multiplier_);

    RetrievalReason* reason = candidate->add_reasons();
    reason->set_reason_type(ReasonType::REASON_TYPE_USER_CF);
    reason->set_reason_score(scored_candidate.reason_score * score_multiplier_);
    reason->set_description(
        format("Retrieved from user_cf using similar user {}.",
               scored_candidate.trigger_entity_id));
    reason->mutable_trigger()->set_entity_type(EntityType::ENTITY_TYPE_USER);
    reason->mutable_trigger()->set_entity_id(scored_candidate.trigger_entity_id);

    RetrievalSignal* signal = candidate->add_retrieval_signals();
    signal->set_retriever(kRetrieverName);
    signal->set_retrieve_score(candidate->retrieve_score());
    signal->add_reasons()->CopyFrom(*reason);
  }
}

vector<TriggerSeed> RetrieverUserCf::CollectTriggerSeeds(
    uint64_t request_user_id,
    const vector<UserNeighbor>& user_neighbors) {
  // Keep valid similar users only and deduplicate by user id.
  vector<TriggerSeed> trigger_seeds;
  trigger_seeds.reserve(user_neighbors.size());
  unordered_set<uint64_t> seen_user_ids;
  for (const UserNeighbor& user_neighbor : user_neighbors) {
    if (user_neighbor.user_id == 0 || user_neighbor.user_id == request_user_id ||
        user_neighbor.score <= 0.0) {
      continue;
    }
    if (!seen_user_ids.insert(user_neighbor.user_id).second) {
      continue;
    }
    trigger_seeds.emplace_back(TriggerSeed{
        .entity_id = user_neighbor.user_id,
        .score = user_neighbor.score,
    });
  }
  return trigger_seeds;
}

}  // namespace shooting_star::recommendation_engine
