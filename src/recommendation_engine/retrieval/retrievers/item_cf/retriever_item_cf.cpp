#include "src/recommendation_engine/retrieval/retrievers/item_cf/retriever_item_cf.h"

#include <cstdint>
#include <format>

namespace recommendation_engine {
namespace {

constexpr char kRetrieverName[] = "item_cf";

using ::grpc::Status;
using ::std::format;
using ::std::vector;
using ::std::unordered_set;

void AppendSeenItems(const ::google::protobuf::RepeatedField<::int64_t>& items,
                     ::std::unordered_set<uint64_t>* seen_items) {
  for (const int64_t item_id : items) {
    if (item_id > 0) {
      seen_items->insert(static_cast<uint64_t>(item_id));
    }
  }
}

}  // namespace

Status RetrieverItemCf::DoRetrieve(const RetrieverRequest& request,
                                   RetrieverResponse* response) const {
  const vector<TriggerSeed> trigger_seeds = CollectTriggerSeeds(request.profile());
  if (trigger_seeds.empty()) {
    response->set_status(RetrieverServiceStatus::RETRIEVER_SUCCESS);
    return Status::OK;
  }

  unordered_set<uint64_t> emitted_item_ids = CollectSeenItems(request.profile());

  for (int trigger_rank = 0;
       trigger_rank < static_cast<int>(trigger_seeds.size()) &&
       response->candidates_size() < request.max_candidate_count();
       ++trigger_rank) {
    const TriggerSeed& trigger_seed = trigger_seeds[trigger_rank];

    for (int neighbor_rank = 0;
         neighbor_rank < request.max_candidate_count() &&
         response->candidates_size() < request.max_candidate_count();
         ++neighbor_rank) {
      const uint64_t candidate_item_id =
          BuildCandidateItemId(trigger_seed.item_id, trigger_rank, neighbor_rank);
      if (!emitted_item_ids.insert(candidate_item_id).second) {
        continue;
      }

      CandidateItem* candidate = response->add_candidates();
      candidate->set_item_id(candidate_item_id);
      candidate->set_item_type(candidate_item_id % 2 == 0 ? ItemType::ITEM_TYPE_VIDEO
                                                          : ItemType::ITEM_TYPE_IMAGE_TEXT);
      candidate->set_author_id(candidate_item_id % 100000 + 1000);
      candidate->set_retriever(kRetrieverName);
      candidate->set_retrieve_score(trigger_seed.score /
                                    static_cast<double>(neighbor_rank + 1));

      RetrievalReason* reason = candidate->add_reasons();
      reason->set_reason_type(ReasonType::REASON_TYPE_ITEM_CF);
      reason->set_reason_score(trigger_seed.score);
      reason->set_description(format("Retrieved from item_cf using trigger item {}.",
                                     trigger_seed.item_id));
      reason->mutable_trigger()->set_entity_type(EntityType::ENTITY_TYPE_ITEM);
      reason->mutable_trigger()->set_entity_id(trigger_seed.item_id);
    }
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
          const ::google::protobuf::RepeatedField<::int64_t>& items, double base_score) {
        int rank = 0;
        for (const int64_t item_id : items) {
          ++rank;
          if (item_id <= 0) {
            continue;
          }

          const uint64_t trigger_item_id = static_cast<uint64_t>(item_id);
          if (!seen_triggers.insert(trigger_item_id).second) {
            continue;
          }

          trigger_seeds.push_back(
              TriggerSeed{trigger_item_id, base_score / static_cast<double>(rank)});
        }
      };

  append_trigger_seeds(profile.session().recent_clicked_items(), 1.0);
  append_trigger_seeds(profile.session().recent_viewed_items(), 0.7);
  append_trigger_seeds(profile.behaviors().clicked_items(), 0.5);
  append_trigger_seeds(profile.behaviors().viewed_items(), 0.3);

  return trigger_seeds;
}

unordered_set<uint64_t> RetrieverItemCf::CollectSeenItems(const Profile& profile) {
  unordered_set<uint64_t> seen_items;

  AppendSeenItems(profile.session().recent_clicked_items(), &seen_items);
  AppendSeenItems(profile.session().recent_viewed_items(), &seen_items);
  AppendSeenItems(profile.behaviors().clicked_items(), &seen_items);
  AppendSeenItems(profile.behaviors().viewed_items(), &seen_items);

  return seen_items;
}

uint64_t RetrieverItemCf::BuildCandidateItemId(uint64_t trigger_item_id,
                                               int trigger_rank,
                                               int neighbor_rank) {
  return trigger_item_id * 1000 + static_cast<uint64_t>((trigger_rank + 1) * 100) +
         static_cast<uint64_t>(neighbor_rank + 1);
}

}  // namespace recommendation_engine
