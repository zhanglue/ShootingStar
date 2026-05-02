#include "src/recommendation_engine/retrieval/retrievers/item_cf/retriever_item_cf.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "src/recommendation_engine/retrieval/retrievers/item_cf/redis_item_similarity_store.h"
#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/redis_client/redis_client.h"

namespace recommendation_engine {
namespace {

constexpr char kRetrieverName[] = "item_cf";

using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::format;
using ::std::make_shared;
using ::std::make_unique;
using ::std::partial_sort;
using ::std::runtime_error;
using ::std::shared_ptr;
using ::std::size_t;
using ::std::sort;
using ::std::to_string;
using ::std::unique_ptr;
using ::std::unordered_map;
using ::std::unordered_set;
using ::std::vector;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::RedisClient;

using TriggerSeed = RetrieverBase::TriggerSeed;
using CandidateScore = RetrieverBase::CandidateScore;

void AppendTriggerSeeds(
    const ::google::protobuf::RepeatedPtrField<WeightedItem>& items,
    double base_score,
    unordered_set<uint64_t>* seen_triggers,
    vector<TriggerSeed>* trigger_seeds) {
  // Convert weighted behavior items into ranked trigger seeds.
  int rank = 0;
  for (const WeightedItem& item : items) {
    ++rank;
    if (item.item_id() <= 0) {
      continue;
    }

    const uint64_t trigger_item_id = static_cast<uint64_t>(item.item_id());
    if (!seen_triggers->insert(trigger_item_id).second) {
      continue;
    }

    const double item_weight = item.weight() > 0.0 ? item.weight() : 1.0;
    trigger_seeds->emplace_back(TriggerSeed{
        .entity_id = trigger_item_id,
        .score = base_score * item_weight / static_cast<double>(rank),
    });
  }
}

void AddCandidateContribution(
    const TriggerSeed& trigger_seed,
    const ItemNeighbor& neighbor,
    unordered_map<uint64_t, CandidateScore>* candidates) {
  // Accumulate score contribution and keep the strongest explanation edge.
  if (neighbor.item_id == 0 || neighbor.score <= 0.0) {
    return;
  }

  const double contribution = trigger_seed.score * neighbor.score;
  if (contribution <= 0.0) {
    return;
  }

  auto [iter, inserted] = candidates->try_emplace(neighbor.item_id);
  CandidateScore& candidate = iter->second;
  if (inserted) {
    candidate.item_id = neighbor.item_id;
  }
  candidate.score += contribution;

  if (inserted || contribution > candidate.reason_score) {
    candidate.trigger_entity_id = trigger_seed.entity_id;
    candidate.trigger_score = trigger_seed.score;
    candidate.source_score = neighbor.score;
    candidate.reason_score = contribution;
  }
}

vector<TriggerSeed> SelectTopTriggerSeeds(
    vector<TriggerSeed> trigger_seeds,
    int max_trigger_seed_count) {
  // Keep only top-N seeds to cap remote lookups and aggregation cost.
  if (max_trigger_seed_count <= 0 ||
      trigger_seeds.size() <=
          static_cast<size_t>(max_trigger_seed_count)) {
    return trigger_seeds;
  }

  auto higher_score_first = [](const TriggerSeed& lhs,
                               const TriggerSeed& rhs) {
    if (lhs.score != rhs.score) {
      return lhs.score > rhs.score;
    }
    return lhs.entity_id < rhs.entity_id;
  };

  const auto middle =
      trigger_seeds.begin() + static_cast<::std::ptrdiff_t>(max_trigger_seed_count);
  partial_sort(trigger_seeds.begin(), middle, trigger_seeds.end(),
               higher_score_first);
  trigger_seeds.resize(static_cast<size_t>(max_trigger_seed_count));
  return trigger_seeds;
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

}  // namespace

RetrieverItemCf::RetrieverItemCf(int default_max_candidate_count)
    : RetrieverItemCf(
          make_unique<RedisItemSimilarityStore>(RedisClient::Create(),
                                                GlobalConfig::Get().GetRedisKeyPrefix()),
          default_max_candidate_count) {}

RetrieverItemCf::RetrieverItemCf(
    unique_ptr<ItemSimilarityStore> item_similarity_store,
    int default_max_candidate_count)
    : RetrieverBase(default_max_candidate_count),
      item_similarity_store_(::std::move(item_similarity_store)) {
  if (item_similarity_store_ == nullptr) {
    throw runtime_error("RetrieverItemCf item_similarity_store must not be null");
  }
}

Status RetrieverItemCf::DoRetrieve(const RetrieverRequest& request,
                                   RetrieverResponse* response) const {
  const auto& logger = LoggerRegistry::Get();
  logger.Debug(
      "item_cf_retrieve_started",
      {
          {"user_id", to_string(request.user_id())},
          {"max_candidate_count", to_string(request.max_candidate_count())},
      });

  const auto session = make_shared<SessionData>();

  // Step 1: Build seeds from profile signals.
  BuildTriggerSeeds(request, session);
  if (session->trigger_seeds.empty()) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_EMPTY_TRIGGER_SEEDS);
    logger.Debug(
        "item_cf_retrieve_early_return_empty_trigger_seeds",
        {
            {"user_id", to_string(request.user_id())},
        });
    return Status::OK;
  }

  // Step 2: Build exclusion set to avoid returning consumed items.
  BuildFilterSet(request.profile(), session);

  // Step 3: Query similarity graph and aggregate candidate scores.
  const Status aggregate_status =
      AggregateSimilarityCandidates(request, session, response);
  if (!aggregate_status.ok()) {
    return aggregate_status;
  }

  // Step 4: Rank and fill response candidates.
  FillResponseCandidates(session, request, response);

  response->set_status(RetrieverServiceStatus::RETRIEVER_SUCCESS);
  logger.Debug(
      "item_cf_retrieve_finished",
      {
          {"user_id", to_string(request.user_id())},
          {"candidate_count", to_string(response->candidates_size())},
      });
  return Status::OK;
}

void RetrieverItemCf::BuildTriggerSeeds(
    const RetrieverRequest& request,
    const shared_ptr<SessionData>& session) const {
  const GlobalConfig& config = GlobalConfig::Get();
  const auto& logger = LoggerRegistry::Get();
  vector<TriggerSeed> collected_trigger_seeds =
      CollectTriggerSeeds(request.profile());
  session->trigger_seeds = SelectTopTriggerSeeds(
      ::std::move(collected_trigger_seeds),
      config.GetRetrieverMaxTriggerSeedCount());

  logger.Debug(
      "item_cf_trigger_seeds_ready",
      {
          {"user_id", to_string(request.user_id())},
          {"trigger_seed_count", to_string(session->trigger_seeds.size())},
          {"max_trigger_seed_count",
           to_string(config.GetRetrieverMaxTriggerSeedCount())},
      });
}

void RetrieverItemCf::BuildFilterSet(
    const Profile& profile,
    const shared_ptr<SessionData>& session) const {
  session->item_ids_to_filter_out = CollectItemIdsToFilterOut(profile);
}

Status RetrieverItemCf::AggregateSimilarityCandidates(
    const RetrieverRequest& request,
    const shared_ptr<SessionData>& session,
    RetrieverResponse* response) const {
  const GlobalConfig& config = GlobalConfig::Get();
  const auto& logger = LoggerRegistry::Get();
  const int redis_command_batch_size = config.GetRedisCommandBatchSize();

  logger.Debug(
      "item_cf_filter_set_ready",
      {
          {"user_id", to_string(request.user_id())},
          {"redis_command_batch_size", to_string(redis_command_batch_size)},
          {"item_ids_to_filter_out_count",
           to_string(session->item_ids_to_filter_out.size())},
      });

  try {
    // Reset session counters and candidate pool for this retrieval.
    session->total_neighbors_fetched = 0;
    session->total_neighbors_filtered_out = 0;
    session->candidates.clear();

    // Fetch neighbors in batches to bound Redis pipeline size.
    for (size_t batch_start = 0; batch_start < session->trigger_seeds.size();
         batch_start += static_cast<size_t>(redis_command_batch_size)) {
      const size_t batch_end =
          ::std::min(batch_start + static_cast<size_t>(redis_command_batch_size),
                     session->trigger_seeds.size());
      const size_t batch_index =
          batch_start / static_cast<size_t>(redis_command_batch_size);
      vector<uint64_t> trigger_item_ids;
      trigger_item_ids.reserve(batch_end - batch_start);
      for (size_t i = batch_start; i < batch_end; ++i) {
        trigger_item_ids.emplace_back(session->trigger_seeds[i].entity_id);
      }

      const vector<vector<ItemNeighbor>> neighbors_by_item_id =
          item_similarity_store_->FindNeighborsByItemIds(
              trigger_item_ids, request.max_candidate_count());
      // Defensive check: store contract must keep order and cardinality.
      if (neighbors_by_item_id.size() != trigger_item_ids.size()) {
        throw runtime_error("ItemSimilarityStore batch lookup returned mismatched result size");
      }

      size_t batch_neighbors_fetched = 0;
      size_t batch_neighbors_filtered_out = 0;
      AggregateSimilarityBatch(neighbors_by_item_id, batch_start, session,
                               &batch_neighbors_fetched,
                               &batch_neighbors_filtered_out);
      session->total_neighbors_fetched += batch_neighbors_fetched;
      session->total_neighbors_filtered_out += batch_neighbors_filtered_out;
      logger.Debug(
          "item_cf_batch_processed",
          {
              {"user_id", to_string(request.user_id())},
              {"batch_index", to_string(batch_index)},
              {"batch_trigger_item_count", to_string(trigger_item_ids.size())},
              {"batch_neighbors_fetched", to_string(batch_neighbors_fetched)},
              {"batch_neighbors_filtered_out",
               to_string(batch_neighbors_filtered_out)},
              {"current_candidate_pool_size",
               to_string(session->candidates.size())},
          });
    }

    logger.Debug(
        "item_cf_similarity_aggregation_done",
        {
            {"user_id", to_string(request.user_id())},
            {"total_neighbors_fetched",
             to_string(session->total_neighbors_fetched)},
            {"total_neighbors_filtered_out",
             to_string(session->total_neighbors_filtered_out)},
            {"candidate_pool_size", to_string(session->candidates.size())},
        });
  } catch (const ::std::exception& ex) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_SYSTEM_ERROR);
    logger.Error(
        "item_cf_similarity_lookup_failed",
        {
            {"user_id", to_string(request.user_id())},
            {"error", ex.what()},
        });
    return Status(StatusCode::INTERNAL, ex.what());
  }
  return Status::OK;
}

void RetrieverItemCf::AggregateSimilarityBatch(
    const vector<vector<ItemNeighbor>>& neighbors_by_item_id,
    size_t batch_start,
    const shared_ptr<SessionData>& session,
    size_t* batch_neighbors_fetched,
    size_t* batch_neighbors_filtered_out) const {
  // For each trigger item, merge valid neighbors into the global candidate map.
  for (size_t i = 0; i < neighbors_by_item_id.size(); ++i) {
    const TriggerSeed& trigger_seed = session->trigger_seeds[batch_start + i];
    *batch_neighbors_fetched += neighbors_by_item_id[i].size();
    for (const ItemNeighbor& neighbor : neighbors_by_item_id[i]) {
      if (session->item_ids_to_filter_out.contains(neighbor.item_id)) {
        ++(*batch_neighbors_filtered_out);
        continue;
      }
      AddCandidateContribution(trigger_seed, neighbor, &session->candidates);
    }
  }
}

void RetrieverItemCf::FillResponseCandidates(
    const shared_ptr<SessionData>& session,
    const RetrieverRequest& request,
    RetrieverResponse* response) const {
  // Convert aggregated map to an ordered top-K response.
  const vector<CandidateScore> sorted_candidates =
      SortCandidates(session->candidates);
  const double score_multiplier =
      GlobalConfig::Get().GetRetrieverItemCfScoreMultiplier();
  for (const CandidateScore& scored_candidate : sorted_candidates) {
    if (response->candidates_size() >= request.max_candidate_count()) {
      break;
    }

    CandidateItem* candidate = response->add_candidates();
    candidate->set_item_id(scored_candidate.item_id);
    candidate->set_item_type(ItemType::ITEM_TYPE_UNSPECIFIED);
    candidate->set_retriever(kRetrieverName);
    candidate->set_retrieve_score(scored_candidate.score * score_multiplier);

    RetrievalReason* reason = candidate->add_reasons();
    reason->set_reason_type(ReasonType::REASON_TYPE_ITEM_CF);
    reason->set_reason_score(scored_candidate.reason_score * score_multiplier);
    reason->set_description(
        format("Retrieved from item_cf using trigger item {}.",
               scored_candidate.trigger_entity_id));
    reason->mutable_trigger()->set_entity_type(EntityType::ENTITY_TYPE_ITEM);
    reason->mutable_trigger()->set_entity_id(scored_candidate.trigger_entity_id);
  }
}

vector<TriggerSeed> RetrieverItemCf::CollectTriggerSeeds(
    const Profile& profile) {
  // Merge multiple behavior channels with different base importance.
  unordered_set<uint64_t> seen_triggers;
  vector<TriggerSeed> trigger_seeds;

  AppendTriggerSeeds(profile.behaviors().recent_liked_items(),
                     1.0,
                     &seen_triggers,
                     &trigger_seeds);
  AppendTriggerSeeds(profile.behaviors().liked_items(),
                     0.5,
                     &seen_triggers,
                     &trigger_seeds);
  AppendTriggerSeeds(profile.behaviors().interested_items(),
                     0.3,
                     &seen_triggers,
                     &trigger_seeds);

  return trigger_seeds;
}

}  // namespace recommendation_engine
