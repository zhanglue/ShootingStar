#include "src/recommendation_engine/retrieval/retrievers/item_cf/redis_item_similarity_store.h"

#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace recommendation_engine {
namespace {

using ::shooting_star::utilities::RedisClient;
using ::shooting_star::utilities::RedisScoredMember;
using ::shooting_star::utilities::RedisScoredMemberListResult;
using ::std::format;
using ::std::invalid_argument;
using ::std::nullopt;
using ::std::optional;
using ::std::runtime_error;
using ::std::string;
using ::std::string_view;
using ::std::vector;

optional<uint64_t> ParseItemId(string_view value) {
  uint64_t item_id = 0;
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, error] = ::std::from_chars(first, last, item_id);
  if (error != ::std::errc() || ptr != last || item_id == 0) {
    return nullopt;
  }
  return item_id;
}

}  // namespace

RedisItemSimilarityStore::RedisItemSimilarityStore(RedisClient client,
                                                   string key_prefix)
    : client_(::std::move(client)), key_prefix_(::std::move(key_prefix)) {
  if (key_prefix_.empty()) {
    throw invalid_argument("RedisItemSimilarityStore key_prefix must not be empty");
  }
}

vector<ItemNeighbor> RedisItemSimilarityStore::FindNeighborsByItemId(
    uint64_t item_id, int max_neighbor_count) const {
  if (item_id == 0 || max_neighbor_count <= 0) {
    return {};
  }

  const string key = RedisClient::BuildRedisKey(key_prefix_, item_id);
  const RedisScoredMemberListResult result =
      client_.ZRevRangeWithScores(key, 0, max_neighbor_count - 1);
  if (!result.status.ok) {
    throw runtime_error(format(
        "Failed to fetch item similarity from Redis key {} after {} attempt(s): {}",
        key, result.status.attempts, result.status.error_message));
  }

  vector<ItemNeighbor> neighbors;
  neighbors.reserve(result.values.size());
  for (const RedisScoredMember& value : result.values) {
    const optional<uint64_t> neighbor_item_id = ParseItemId(value.member);
    if (!neighbor_item_id.has_value()) {
      throw runtime_error(format(
          "Redis item similarity key {} contains invalid neighbor item id: {}",
          key, value.member));
    }
    neighbors.push_back(ItemNeighbor{
        .item_id = *neighbor_item_id,
        .score = value.score,
    });
  }
  return neighbors;
}

vector<vector<ItemNeighbor>> RedisItemSimilarityStore::FindNeighborsByItemIds(
    const vector<uint64_t>& item_ids, int max_neighbor_count) const {
  vector<vector<ItemNeighbor>> neighbors_by_item_id;
  neighbors_by_item_id.resize(item_ids.size());
  if (item_ids.empty() || max_neighbor_count <= 0) {
    return neighbors_by_item_id;
  }

  vector<::std::size_t> valid_item_indexes;
  vector<string> redis_keys;
  valid_item_indexes.reserve(item_ids.size());
  redis_keys.reserve(item_ids.size());
  for (::std::size_t i = 0; i < item_ids.size(); ++i) {
    if (item_ids[i] == 0) {
      continue;
    }
    valid_item_indexes.push_back(i);
    redis_keys.push_back(RedisClient::BuildRedisKey(key_prefix_, item_ids[i]));
  }

  if (redis_keys.empty()) {
    return neighbors_by_item_id;
  }

  const auto batch_result = client_.BatchZRevRangeWithScores(
      redis_keys, 0, max_neighbor_count - 1);
  if (!batch_result.status.ok) {
    throw runtime_error(format(
        "Failed to fetch item similarity from Redis in batch after {} attempt(s): {}",
        batch_result.status.attempts,
        batch_result.status.error_message));
  }
  if (batch_result.values.size() != valid_item_indexes.size()) {
    throw runtime_error(
        "Redis item similarity batch lookup returned mismatched result size");
  }

  for (::std::size_t i = 0; i < batch_result.values.size(); ++i) {
    vector<ItemNeighbor> neighbors;
    neighbors.reserve(batch_result.values[i].size());
    for (const RedisScoredMember& value : batch_result.values[i]) {
      const optional<uint64_t> neighbor_item_id = ParseItemId(value.member);
      if (!neighbor_item_id.has_value()) {
        throw runtime_error(format(
            "Redis item similarity key {} contains invalid neighbor item id: {}",
            redis_keys[i], value.member));
      }
      neighbors.push_back(ItemNeighbor{
          .item_id = *neighbor_item_id,
          .score = value.score,
      });
    }
    neighbors_by_item_id[valid_item_indexes[i]] = ::std::move(neighbors);
  }
  return neighbors_by_item_id;
}

}  // namespace recommendation_engine
