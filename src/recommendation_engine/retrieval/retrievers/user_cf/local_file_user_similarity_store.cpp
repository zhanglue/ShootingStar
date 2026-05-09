#include "src/recommendation_engine/retrieval/retrievers/user_cf/local_file_user_similarity_store.h"

#include <algorithm>
#include <fstream>
#include <format>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "src/recommendation_engine/retrieval/retrievers/base/local_file_similarity_jsonl_parser.h"

namespace recommendation_engine {
namespace {

using ::std::format;
using ::std::ifstream;
using ::std::runtime_error;
using ::std::string;
using ::std::unordered_map;
using ::std::vector;

vector<UserNeighbor> ToUserNeighbors(
    const vector<LocalFileSimilarityNeighbor>& parsed_neighbors) {
  vector<UserNeighbor> neighbors;
  neighbors.reserve(parsed_neighbors.size());
  for (const LocalFileSimilarityNeighbor& parsed_neighbor : parsed_neighbors) {
    neighbors.emplace_back(UserNeighbor{
        .user_id = parsed_neighbor.id,
        .score = parsed_neighbor.score,
    });
  }
  return neighbors;
}

}  // namespace

LocalFileUserSimilarityStore::LocalFileUserSimilarityStore(const string& file_path) {
  LoadFromJsonlFile(file_path);
}

void LocalFileUserSimilarityStore::LoadFromJsonlFile(const string& file_path) {
  ifstream fin(file_path);
  if (!fin.is_open()) {
    throw runtime_error(
        "LocalFileUserSimilarityStore::LoadFromJsonlFile failed: cannot open file " +
        file_path);
  }

  unordered_map<uint64_t, vector<UserNeighbor>> loaded_neighbors;
  string line;
  int line_number = 0;
  while (::std::getline(fin, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }

    const string context = format("{}:{}", file_path, line_number);
    const LocalFileSimilarityRow row =
        ParseLocalFileSimilarityJsonlLine(line, "user_id", "user_id", context);
    loaded_neighbors[row.entity_id] = ToUserNeighbors(row.neighbors);
  }

  neighbors_by_user_id_ = ::std::move(loaded_neighbors);
}

vector<UserNeighbor> LocalFileUserSimilarityStore::FindNeighborsByUserId(
    uint64_t user_id, int max_neighbor_count) const {
  if (user_id == 0 || max_neighbor_count <= 0) {
    return {};
  }

  const auto iter = neighbors_by_user_id_.find(user_id);
  if (iter == neighbors_by_user_id_.end()) {
    return {};
  }

  const size_t count = ::std::min(iter->second.size(),
                                 static_cast<size_t>(max_neighbor_count));
  return vector<UserNeighbor>(iter->second.begin(), iter->second.begin() + count);
}

}  // namespace recommendation_engine
