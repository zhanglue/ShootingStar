#pragma once

#include <cstdint>
#include <vector>

namespace recommendation_engine::user_cf {

struct UserNeighbor {
  uint64_t user_id = 0;
  double score = 0.0;
};

class UserSimilarityStore {
 public:
  virtual ~UserSimilarityStore() = default;

  virtual ::std::vector<UserNeighbor> FindNeighborsByUserId(
      uint64_t user_id, int max_neighbor_count) const = 0;
};

}  // namespace recommendation_engine::user_cf
