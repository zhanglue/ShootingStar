#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"

namespace recommendation_engine {

class RetrieverUserCf final : public RetrieverBase {
 public:
  explicit RetrieverUserCf(int default_max_candidate_count = 10)
      : RetrieverBase(default_max_candidate_count) {}

  struct TriggerSeed {
    uint64_t author_id;
    double score;
  };

 private:
  ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                            RetrieverResponse* response) const override;

  static ::std::vector<TriggerSeed> CollectTriggerSeeds(const Profile& profile);
  static ::std::unordered_set<uint64_t> CollectItemIdsToFilterOut(
      const Profile& profile);
  static uint64_t BuildCandidateItemId(uint64_t author_id, int trigger_rank, int candidate_rank);
};

}  // namespace recommendation_engine
