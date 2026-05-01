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

  virtual ::std::vector<::std::vector<ItemNeighbor>> FindNeighborsByItemIds(
      const ::std::vector<uint64_t>& item_ids, int max_neighbor_count) const {
    ::std::vector<::std::vector<ItemNeighbor>> neighbors_by_item_id;
    neighbors_by_item_id.reserve(item_ids.size());
    for (const uint64_t item_id : item_ids) {
      neighbors_by_item_id.emplace_back(
          FindNeighborsByItemId(item_id, max_neighbor_count));
    }
    return neighbors_by_item_id;
  }
};

}  // namespace recommendation_engine
