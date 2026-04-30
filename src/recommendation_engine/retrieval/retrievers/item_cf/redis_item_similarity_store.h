#pragma once

#include <string>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/item_cf/item_similarity_store.h"
#include "src/utilities/redis_client/redis_client.h"

namespace recommendation_engine {

class RedisItemSimilarityStore final : public ItemSimilarityStore {
 public:
  RedisItemSimilarityStore(::shooting_star::utilities::RedisClient client,
                           ::std::string key_prefix);

  ::std::vector<ItemNeighbor> FindNeighborsByItemId(
      uint64_t item_id, int max_neighbor_count) const override;
  ::std::vector<::std::vector<ItemNeighbor>> FindNeighborsByItemIds(
      const ::std::vector<uint64_t>& item_ids,
      int max_neighbor_count) const override;

 private:
  ::shooting_star::utilities::RedisClient client_;
  ::std::string key_prefix_;
};

}  // namespace recommendation_engine
