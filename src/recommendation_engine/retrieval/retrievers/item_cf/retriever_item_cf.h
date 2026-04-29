#pragma once

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/item_cf/item_similarity_store.h"
#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"

namespace recommendation_engine {

class RetrieverItemCf final : public RetrieverBase {
 public:
  explicit RetrieverItemCf(int default_max_candidate_count = 10);
  RetrieverItemCf(::std::unique_ptr<ItemSimilarityStore> item_similarity_store,
                  int default_max_candidate_count = 10);

  struct TriggerSeed {
    uint64_t item_id;
    double score;
  };

  struct CandidateScore {
    uint64_t item_id = 0;
    double score = 0.0;
    uint64_t trigger_item_id = 0;
    double trigger_score = 0.0;
    double similarity_score = 0.0;
    double reason_score = 0.0;
  };

 private:
  ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                            RetrieverResponse* response) const override;

  static ::std::vector<TriggerSeed> CollectTriggerSeeds(const Profile& profile);
  static ::std::unordered_set<uint64_t> CollectSeenItems(const Profile& profile);

  ::std::unique_ptr<ItemSimilarityStore> item_similarity_store_;
};

}  // namespace recommendation_engine
