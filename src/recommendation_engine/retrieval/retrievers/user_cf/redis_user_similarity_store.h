#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/user_cf/user_similarity_store.h"
#include "src/utilities/redis_client/redis_client.h"

namespace recommendation_engine {

class RedisUserSimilarityStore final : public UserSimilarityStore {
 public:
  RedisUserSimilarityStore(::shooting_star::utilities::RedisClient client,
                           ::std::string key_prefix);

  ::std::vector<UserNeighbor> FindNeighborsByUserId(
      uint64_t user_id, int max_neighbor_count) const override;

 private:
  ::shooting_star::utilities::RedisClient client_;
  ::std::string key_prefix_;
};

}  // namespace recommendation_engine
