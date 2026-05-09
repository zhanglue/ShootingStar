#include "src/recommendation_engine/retrieval/retrievers/user_cf/redis_user_similarity_store.h"

#include <charconv>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace shooting_star::recommendation_engine {
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

optional<uint64_t> ParseUserId(string_view value) {
  uint64_t user_id = 0;
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, error] = ::std::from_chars(first, last, user_id);
  if (error != ::std::errc() || ptr != last || user_id == 0) {
    return nullopt;
  }
  return user_id;
}

}  // namespace

RedisUserSimilarityStore::RedisUserSimilarityStore(RedisClient client,
                                                   string key_prefix)
    : client_(::std::move(client)), key_prefix_(::std::move(key_prefix)) {
  if (key_prefix_.empty()) {
    throw invalid_argument("RedisUserSimilarityStore key_prefix must not be empty");
  }
}

vector<UserNeighbor> RedisUserSimilarityStore::FindNeighborsByUserId(
    uint64_t user_id, int max_neighbor_count) const {
  if (user_id == 0 || max_neighbor_count <= 0) {
    return {};
  }

  const string key = RedisClient::BuildRedisKey(key_prefix_, user_id);
  const RedisScoredMemberListResult result =
      client_.ZRevRangeWithScores(key, 0, max_neighbor_count - 1);
  if (!result.status.ok) {
    throw runtime_error(format(
        "Failed to fetch user similarity from Redis key {} after {} attempt(s): {}",
        key, result.status.attempts, result.status.error_message));
  }

  vector<UserNeighbor> neighbors;
  neighbors.reserve(result.values.size());
  for (const RedisScoredMember& value : result.values) {
    const optional<uint64_t> neighbor_user_id = ParseUserId(value.member);
    if (!neighbor_user_id.has_value()) {
      throw runtime_error(format(
          "Redis user similarity key {} contains invalid neighbor user id: {}",
          key, value.member));
    }
    neighbors.emplace_back(UserNeighbor{
        .user_id = *neighbor_user_id,
        .score = value.score,
    });
  }
  return neighbors;
}

}  // namespace shooting_star::recommendation_engine
