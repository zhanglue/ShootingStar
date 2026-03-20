#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"

namespace recommendation_engine {

class RetrieverItemCf final : public RetrieverBase {
 public:
  explicit RetrieverItemCf(int default_max_candidate_count = 10)
      : RetrieverBase(default_max_candidate_count) {}

 private:
  ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                            RetrieverResponse* response) const override;

  struct TriggerSeed {
    uint64_t item_id;
    double score;
  };

  static ::std::vector<TriggerSeed> CollectTriggerSeeds(const Profile& profile);
  static ::std::unordered_set<uint64_t> CollectSeenItems(const Profile& profile);
  static uint64_t BuildCandidateItemId(uint64_t trigger_item_id,
                                       int trigger_rank,
                                       int neighbor_rank);
};

}  // namespace recommendation_engine
