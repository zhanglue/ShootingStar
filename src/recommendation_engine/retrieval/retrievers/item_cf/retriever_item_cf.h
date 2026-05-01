#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/item_cf/item_similarity_store.h"
#include "src/recommendation_engine/retrieval/retrievers/base/retriever_base.h"

namespace recommendation_engine {

class RetrieverItemCf final : public RetrieverBase {
 public:
  explicit RetrieverItemCf(int default_max_candidate_count = 10);
  RetrieverItemCf(::std::unique_ptr<ItemSimilarityStore> item_similarity_store,
                  int default_max_candidate_count = 10);

 private:
  ::grpc::Status DoRetrieve(const RetrieverRequest& request,
                            RetrieverResponse* response) const override;

  static ::std::vector<RetrieverBase::TriggerSeed> CollectTriggerSeeds(
      const Profile& profile);

  ::std::unique_ptr<ItemSimilarityStore> item_similarity_store_;
};

}  // namespace recommendation_engine
