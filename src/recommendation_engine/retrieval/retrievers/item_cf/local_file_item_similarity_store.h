#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/item_cf/item_similarity_store.h"

namespace shooting_star::recommendation_engine {

class LocalFileItemSimilarityStore final : public ItemSimilarityStore {
 public:
  explicit LocalFileItemSimilarityStore(const ::std::string& file_path);

  ::std::vector<ItemNeighbor> FindNeighborsByItemId(
      uint64_t item_id, int max_neighbor_count) const override;

 private:
  void LoadFromJsonlFile(const ::std::string& file_path);

  ::std::unordered_map<uint64_t, ::std::vector<ItemNeighbor>> neighbors_by_item_id_;
};

}  // namespace shooting_star::recommendation_engine
