#pragma once

#include <cstdint>
#include <vector>

namespace recommendation_engine {

struct ItemNeighbor {
  uint64_t item_id = 0;
  double score = 0.0;
};

class ItemSimilarityStore {
 public:
  virtual ~ItemSimilarityStore() = default;

  virtual ::std::vector<ItemNeighbor> FindNeighborsByItemId(
      uint64_t item_id, int max_neighbor_count) const = 0;
};

}  // namespace recommendation_engine
