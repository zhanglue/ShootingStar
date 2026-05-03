#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "src/recommendation_engine/retrieval/retrievers/user_cf/user_similarity_store.h"

namespace recommendation_engine::user_cf {

class LocalFileUserSimilarityStore final : public UserSimilarityStore {
 public:
  explicit LocalFileUserSimilarityStore(const ::std::string& file_path);

  ::std::vector<UserNeighbor> FindNeighborsByUserId(
      uint64_t user_id, int max_neighbor_count) const override;

 private:
  void LoadFromJsonlFile(const ::std::string& file_path);

  ::std::unordered_map<uint64_t, ::std::vector<UserNeighbor>> neighbors_by_user_id_;
};

}  // namespace recommendation_engine::user_cf
