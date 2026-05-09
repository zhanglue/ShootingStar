#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace shooting_star::recommendation_engine {

struct UserNeighbor {
  uint64_t user_id = 0;
  double score = 0.0;
};

class UserSimilarityStore {
 public:
  static constexpr ::std::string_view kLocalStoreType = "local";
  static constexpr ::std::string_view kRedisStoreType = "redis";

  virtual ~UserSimilarityStore() = default;

  virtual ::std::vector<UserNeighbor> FindNeighborsByUserId(
      uint64_t user_id, int max_neighbor_count) const = 0;
};

}  // namespace shooting_star::recommendation_engine
