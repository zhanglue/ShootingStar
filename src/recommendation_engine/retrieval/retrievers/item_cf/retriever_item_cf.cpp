#include "src/recommendation_engine/retrieval/retrievers/item_cf/retriever_item_cf.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
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
using ::std::make_unique;
using ::std::runtime_error;
using ::std::sort;
using ::std::string;
using ::std::to_string;
using ::std::unique_ptr;
using ::std::unordered_map;
using ::std::unordered_set;
using ::std::vector;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::RedisClient;

void AppendSeenItems(const ::google::protobuf::RepeatedPtrField<WeightedItem>& items,
                     ::std::unordered_set<uint64_t>* rated_items) {
  for (const WeightedItem& item : items) {
    if (item.item_id() > 0) {
      rated_items->insert(static_cast<uint64_t>(item.item_id()));
    }
  }
}

void AppendSeenItems(const ::google::protobuf::RepeatedField<::int64_t>& items,
                     ::std::unordered_set<uint64_t>* rated_items) {
  for (const int64_t item_id : items) {
    if (item_id > 0) {
      rated_items->insert(static_cast<uint64_t>(item_id));
    }
  }
}

void AddCandidateContribution(
    const RetrieverItemCf::TriggerSeed& trigger_seed,
    const ItemNeighbor& neighbor,
    unordered_map<uint64_t, RetrieverItemCf::CandidateScore>* candidates) {
  if (neighbor.item_id == 0 || neighbor.score <= 0.0) {
    return;
  }

  const double contribution = trigger_seed.score * neighbor.score;
  if (contribution <= 0.0) {
    return;
  }

  auto [iter, inserted] = candidates->try_emplace(neighbor.item_id);
  RetrieverItemCf::CandidateScore& candidate = iter->second;
  if (inserted) {
    candidate.item_id = neighbor.item_id;
  }
  candidate.score += contribution;

  if (inserted || contribution > candidate.reason_score) {
    candidate.trigger_item_id = trigger_seed.item_id;
    candidate.trigger_score = trigger_seed.score;
    candidate.similarity_score = neighbor.score;
    candidate.reason_score = contribution;
  }
}

vector<RetrieverItemCf::CandidateScore> SortCandidates(
    const unordered_map<uint64_t, RetrieverItemCf::CandidateScore>& candidates) {
  vector<RetrieverItemCf::CandidateScore> sorted_candidates;
  sorted_candidates.reserve(candidates.size());
  for (const auto& [_, candidate] : candidates) {
    sorted_candidates.push_back(candidate);
  }

  sort(sorted_candidates.begin(), sorted_candidates.end(),
       [](const RetrieverItemCf::CandidateScore& lhs,
          const RetrieverItemCf::CandidateScore& rhs) {
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
  const vector<TriggerSeed> trigger_seeds = CollectTriggerSeeds(request.profile());
  if (trigger_seeds.empty()) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_SUCCESS);
    return Status::OK;
  }

  const unordered_set<uint64_t> seen_item_ids = CollectSeenItems(request.profile());
  unordered_map<uint64_t, CandidateScore> candidates;

  try {
    for (const TriggerSeed& trigger_seed : trigger_seeds) {
      const vector<ItemNeighbor> neighbors =
          item_similarity_store_->FindNeighborsByItemId(
              trigger_seed.item_id, request.max_candidate_count());
      for (const ItemNeighbor& neighbor : neighbors) {
        if (seen_item_ids.contains(neighbor.item_id)) {
          continue;
        }
        AddCandidateContribution(trigger_seed, neighbor, &candidates);
      }
    }
  } catch (const ::std::exception& ex) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_SYSTEM_ERROR);
    LoggerRegistry::Get().Error(
        "item_cf_similarity_lookup_failed",
        {
            {"user_id", to_string(request.user_id())},
            {"error", ex.what()},
        });
    return Status(StatusCode::INTERNAL, ex.what());
  }

  const vector<CandidateScore> sorted_candidates = SortCandidates(candidates);
  for (const CandidateScore& scored_candidate : sorted_candidates) {
    if (response->candidates_size() >= request.max_candidate_count()) {
      break;
    }

    CandidateItem* candidate = response->add_candidates();
    candidate->set_item_id(scored_candidate.item_id);
    candidate->set_item_type(ItemType::ITEM_TYPE_UNSPECIFIED);
    candidate->set_retriever(kRetrieverName);
    candidate->set_retrieve_score(scored_candidate.score);

    RetrievalReason* reason = candidate->add_reasons();
    reason->set_reason_type(ReasonType::REASON_TYPE_ITEM_CF);
    reason->set_reason_score(scored_candidate.reason_score);
    reason->set_description(
        format("Retrieved from item_cf using trigger item {}.",
               scored_candidate.trigger_item_id));
    reason->mutable_trigger()->set_entity_type(EntityType::ENTITY_TYPE_ITEM);
    reason->mutable_trigger()->set_entity_id(scored_candidate.trigger_item_id);
  }

  response->set_status(RetrieverServiceStatus::RETRIEVER_SUCCESS);
  return Status::OK;
}

vector<RetrieverItemCf::TriggerSeed> RetrieverItemCf::CollectTriggerSeeds(
    const Profile& profile) {
  unordered_set<uint64_t> seen_triggers;
  vector<TriggerSeed> trigger_seeds;

  const auto append_trigger_seeds =
      [&seen_triggers, &trigger_seeds](
          const ::google::protobuf::RepeatedPtrField<WeightedItem>& items, double base_score) {
        int rank = 0;
        for (const WeightedItem& item : items) {
          ++rank;
          if (item.item_id() <= 0) {
            continue;
          }

          const uint64_t trigger_item_id = static_cast<uint64_t>(item.item_id());
          if (!seen_triggers.insert(trigger_item_id).second) {
            continue;
          }

          const double item_weight = item.weight() > 0.0 ? item.weight() : 1.0;
          trigger_seeds.push_back(
              TriggerSeed{trigger_item_id,
                          base_score * item_weight / static_cast<double>(rank)});
        }
      };

  append_trigger_seeds(profile.behaviors().recent_liked_items(), 1.0);
  append_trigger_seeds(profile.behaviors().liked_items(), 0.5);
  append_trigger_seeds(profile.behaviors().interested_items(), 0.3);

  return trigger_seeds;
}

unordered_set<uint64_t> RetrieverItemCf::CollectSeenItems(const Profile& profile) {
  unordered_set<uint64_t> rated_items;

  AppendSeenItems(profile.behaviors().recent_liked_items(), &rated_items);
  AppendSeenItems(profile.behaviors().liked_items(), &rated_items);
  AppendSeenItems(profile.behaviors().interested_items(), &rated_items);
  AppendSeenItems(profile.behaviors().rated_items(), &rated_items);
  AppendSeenItems(profile.negative_feedbacks().items(), &rated_items);

  return rated_items;
}

}  // namespace recommendation_engine
