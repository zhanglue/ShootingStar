#include "src/recommendation_engine/retrieval/retrievers/user_cf/retriever_user_cf.h"

#include <cstdint>
#include <format>

namespace recommendation_engine {
namespace {

constexpr char kRetrieverName[] = "user_cf";

using ::grpc::Status;
using ::std::format;
using ::std::unordered_set;
using ::std::vector;

void AppendSeenItems(const ::google::protobuf::RepeatedField<::int64_t>& items,
                     unordered_set<uint64_t>* seen_items) {
  for (const int64_t item_id : items) {
    if (item_id > 0) {
      seen_items->insert(static_cast<uint64_t>(item_id));
    }
  }
}

}  // namespace

Status RetrieverUserCf::DoRetrieve(const RetrieverRequest& request,
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

    for (int candidate_rank = 0;
         candidate_rank < request.max_candidate_count() &&
         response->candidates_size() < request.max_candidate_count();
         ++candidate_rank) {
      const uint64_t candidate_item_id =
          BuildCandidateItemId(trigger_seed.author_id, trigger_rank, candidate_rank);
      if (!emitted_item_ids.insert(candidate_item_id).second) {
        continue;
      }

      CandidateItem* candidate = response->add_candidates();
      candidate->set_item_id(candidate_item_id);
      candidate->set_item_type(candidate_item_id % 2 == 0 ? ItemType::ITEM_TYPE_VIDEO
                                                          : ItemType::ITEM_TYPE_IMAGE_TEXT);
      candidate->set_author_id(trigger_seed.author_id);
      candidate->set_retriever(kRetrieverName);
      candidate->set_retrieve_score(trigger_seed.score /
                                    static_cast<double>(candidate_rank + 1));

      RetrievalReason* reason = candidate->add_reasons();
      reason->set_reason_type(ReasonType::REASON_TYPE_USER_CF);
      reason->set_reason_score(trigger_seed.score);
      reason->set_description(
          format("Retrieved from user_cf using author {}.", trigger_seed.author_id));
      reason->mutable_trigger()->set_entity_type(EntityType::ENTITY_TYPE_AUTHOR);
      reason->mutable_trigger()->set_entity_id(trigger_seed.author_id);
    }
  }

  response->set_status(RetrieverServiceStatus::RETRIEVER_SUCCESS);
  return Status::OK;
}

vector<RetrieverUserCf::TriggerSeed> RetrieverUserCf::CollectTriggerSeeds(
    const Profile& profile) {
  unordered_set<uint64_t> seen_authors;
  vector<TriggerSeed> trigger_seeds;

  const auto append_trigger_seeds =
      [&seen_authors, &trigger_seeds](
          const ::google::protobuf::RepeatedField<::int64_t>& authors, double base_score) {
        int rank = 0;
        for (const int64_t author_id : authors) {
          ++rank;
          if (author_id <= 0) {
            continue;
          }

          const uint64_t trigger_author_id = static_cast<uint64_t>(author_id);
          if (!seen_authors.insert(trigger_author_id).second) {
            continue;
          }

          trigger_seeds.push_back(
              TriggerSeed{trigger_author_id, base_score / static_cast<double>(rank)});
        }
      };

  append_trigger_seeds(profile.social().following(), 1.0);
  append_trigger_seeds(profile.social().followers(), 0.6);

  return trigger_seeds;
}

unordered_set<uint64_t> RetrieverUserCf::CollectSeenItems(const Profile& profile) {
  unordered_set<uint64_t> seen_items;

  AppendSeenItems(profile.session().recent_clicked_items(), &seen_items);
  AppendSeenItems(profile.session().recent_viewed_items(), &seen_items);
  AppendSeenItems(profile.behaviors().clicked_items(), &seen_items);
  AppendSeenItems(profile.behaviors().viewed_items(), &seen_items);

  return seen_items;
}

uint64_t RetrieverUserCf::BuildCandidateItemId(uint64_t author_id,
                                               int trigger_rank,
                                               int candidate_rank) {
  return author_id * 1000 + static_cast<uint64_t>((trigger_rank + 1) * 100) +
         static_cast<uint64_t>(candidate_rank + 1);
}

}  // namespace recommendation_engine
