/*
 * RedisClient is the project-level wrapper around redis-plus-plus. It keeps
 * Redis dependency details in one place: connection pool sizing, timeout
 * defaults, retry policy, exception normalization, and the small command
 * surface currently needed by recommender services.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace shooting_star {
namespace utilities {

enum class RedisErrorCode {
  kNone,
  kInvalidArgument,
  kIoError,
  kTimeout,
  kClosed,
  kReplyError,
  kProtocolError,
  kUnknown,
};

struct RedisStatus {
  bool ok = false;
  RedisErrorCode error_code = RedisErrorCode::kNone;
  ::std::string error_message;
  int attempts = 0;
};

struct RedisStringResult {
  RedisStatus status;
  ::std::optional<::std::string> value;
};

struct RedisStringListResult {
  RedisStatus status;
  ::std::vector<::std::optional<::std::string>> values;
};

struct RedisScoredMember {
  ::std::string member;
  double score = 0.0;
};

struct RedisScoredMemberListResult {
  RedisStatus status;
  ::std::vector<RedisScoredMember> values;
};

class RedisClient {
 public:
  struct RetryConfig {
    RetryConfig();

    int max_attempts;
    ::std::chrono::milliseconds delay;
  };

  struct Config {
    Config();

    ::std::string host;
    int port;
    int db;
    ::std::string user;
    ::std::string password;
    bool keep_alive;

    ::std::chrono::milliseconds connect_timeout;
    ::std::chrono::milliseconds socket_timeout;

    ::std::size_t pool_size;
    ::std::chrono::milliseconds pool_wait_timeout;
    ::std::chrono::milliseconds pool_connection_lifetime;
    ::std::chrono::milliseconds pool_connection_idle_time;

    RetryConfig retry;
  };

  explicit RedisClient(Config config);

  RedisStatus Ping() const;
  RedisStringResult Get(::std::string key) const;
  RedisStringListResult MGet(const ::std::vector<::std::string>& keys) const;
  RedisScoredMemberListResult ZRevRangeWithScores(::std::string key,
                                                  long long start,
                                                  long long stop) const;

 private:
  class Impl;

  Config config_;
  ::std::shared_ptr<Impl> impl_;
};

}  // namespace utilities
}  // namespace shooting_star
