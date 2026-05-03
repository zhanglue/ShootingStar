#include "src/recommendation_engine/retrieval/retrievers/item_cf/local_file_item_similarity_store.h"

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

vector<ItemNeighbor> ToItemNeighbors(
    const vector<LocalFileSimilarityNeighbor>& parsed_neighbors) {
  vector<ItemNeighbor> neighbors;
  neighbors.reserve(parsed_neighbors.size());
  for (const LocalFileSimilarityNeighbor& parsed_neighbor : parsed_neighbors) {
    neighbors.emplace_back(ItemNeighbor{
        .item_id = parsed_neighbor.id,
        .score = parsed_neighbor.score,
    });
  }
  return neighbors;
}

}  // namespace

LocalFileItemSimilarityStore::LocalFileItemSimilarityStore(const string& file_path) {
  LoadFromJsonlFile(file_path);
}

void LocalFileItemSimilarityStore::LoadFromJsonlFile(const string& file_path) {
  ifstream fin(file_path);
  if (!fin.is_open()) {
    throw runtime_error(
        "LocalFileItemSimilarityStore::LoadFromJsonlFile failed: cannot open file " +
        file_path);
  }

  unordered_map<uint64_t, vector<ItemNeighbor>> loaded_neighbors;
  string line;
  int line_number = 0;
  while (::std::getline(fin, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }

    const string context = format("{}:{}", file_path, line_number);
    const LocalFileSimilarityRow row =
        ParseLocalFileSimilarityJsonlLine(line, "item_id", "item_id", context);
    loaded_neighbors[row.entity_id] = ToItemNeighbors(row.neighbors);
  }

  neighbors_by_item_id_ = ::std::move(loaded_neighbors);
}

vector<ItemNeighbor> LocalFileItemSimilarityStore::FindNeighborsByItemId(
    uint64_t item_id, int max_neighbor_count) const {
  if (item_id == 0 || max_neighbor_count <= 0) {
    return {};
  }

  const auto iter = neighbors_by_item_id_.find(item_id);
  if (iter == neighbors_by_item_id_.end()) {
    return {};
  }

  const size_t count = ::std::min(iter->second.size(),
                                 static_cast<size_t>(max_neighbor_count));
  return vector<ItemNeighbor>(iter->second.begin(), iter->second.begin() + count);
}

}  // namespace recommendation_engine
