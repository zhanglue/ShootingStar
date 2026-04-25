#include "src/utilities/redis_client/redis_client.h"

#include <charconv>
#include <chrono>
#include <memory>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <sw/redis++/redis++.h>

namespace shooting_star {
namespace utilities {

using ::std::chrono::milliseconds;
using ::std::make_shared;
using ::std::optional;
using ::std::shared_ptr;
using ::std::string;
using ::std::string_view;
using ::std::vector;

namespace {

constexpr int kDefaultPort = 6379;
constexpr int kDefaultConnectTimeoutMs = 100;
constexpr int kDefaultSocketTimeoutMs = 100;
constexpr int kDefaultPoolWaitTimeoutMs = 50;
constexpr int kDefaultRetryDelayMs = 10;
constexpr int kDefaultRetryMaxAttempts = 2;
constexpr ::std::size_t kDefaultPoolSize = 4;

void ValidateNonNegativeTimeout(string_view name, milliseconds value) {
  if (value.count() < 0) {
    throw ::std::invalid_argument(string(name) + " must not be negative");
  }
}

void ValidateRedisConfig(const RedisClient::Config& config) {
  if (config.host.empty()) {
    throw ::std::invalid_argument("RedisClient host must not be empty");
  }
  if (config.port <= 0 || config.port > 65535) {
    throw ::std::invalid_argument("RedisClient port must be between 1 and 65535");
  }
  if (config.db < 0) {
    throw ::std::invalid_argument("RedisClient db must not be negative");
  }
  if (config.pool_size == 0) {
    throw ::std::invalid_argument("RedisClient pool_size must be greater than zero");
  }
  if (config.retry.max_attempts <= 0) {
    throw ::std::invalid_argument(
        "RedisClient retry.max_attempts must be greater than zero");
  }

  ValidateNonNegativeTimeout("RedisClient connect_timeout",
                             config.connect_timeout);
  ValidateNonNegativeTimeout("RedisClient socket_timeout", config.socket_timeout);
  ValidateNonNegativeTimeout("RedisClient pool_wait_timeout",
                             config.pool_wait_timeout);
  ValidateNonNegativeTimeout("RedisClient pool_connection_lifetime",
                             config.pool_connection_lifetime);
  ValidateNonNegativeTimeout("RedisClient pool_connection_idle_time",
                             config.pool_connection_idle_time);
  ValidateNonNegativeTimeout("RedisClient retry.delay", config.retry.delay);
}

sw::redis::ConnectionOptions BuildConnectionOptions(
    const RedisClient::Config& config) {
  sw::redis::ConnectionOptions options;
  options.host = config.host;
  options.port = config.port;
  options.db = config.db;
  if (!config.user.empty()) {
    options.user = config.user;
  }
  options.password = config.password;
  options.keep_alive = config.keep_alive;
  options.connect_timeout = config.connect_timeout;
  options.socket_timeout = config.socket_timeout;
  return options;
}

sw::redis::ConnectionPoolOptions BuildConnectionPoolOptions(
    const RedisClient::Config& config) {
  sw::redis::ConnectionPoolOptions options;
  options.size = config.pool_size;
  options.wait_timeout = config.pool_wait_timeout;
  options.connection_lifetime = config.pool_connection_lifetime;
  options.connection_idle_time = config.pool_connection_idle_time;
  return options;
}

RedisStatus OkStatus(int attempts) {
  return RedisStatus{
      .ok = true,
      .error_code = RedisErrorCode::kNone,
      .error_message = "",
      .attempts = attempts,
  };
}

RedisStatus ErrorStatus(RedisErrorCode code, string message, int attempts) {
  return RedisStatus{
      .ok = false,
      .error_code = code,
      .error_message = ::std::move(message),
      .attempts = attempts,
  };
}

bool IsRetryable(RedisErrorCode code) {
  return code == RedisErrorCode::kIoError ||
         code == RedisErrorCode::kTimeout ||
         code == RedisErrorCode::kClosed;
}

RedisStatus StatusFromException(const sw::redis::TimeoutError& error,
                                int attempts) {
  return ErrorStatus(RedisErrorCode::kTimeout, error.what(), attempts);
}

RedisStatus StatusFromException(const sw::redis::ClosedError& error,
                                int attempts) {
  return ErrorStatus(RedisErrorCode::kClosed, error.what(), attempts);
}

RedisStatus StatusFromException(const sw::redis::IoError& error, int attempts) {
  return ErrorStatus(RedisErrorCode::kIoError, error.what(), attempts);
}

RedisStatus StatusFromException(const sw::redis::ReplyError& error,
                                int attempts) {
  return ErrorStatus(RedisErrorCode::kReplyError, error.what(), attempts);
}

RedisStatus StatusFromException(const sw::redis::ProtoError& error,
                                int attempts) {
  return ErrorStatus(RedisErrorCode::kProtocolError, error.what(), attempts);
}

RedisStatus StatusFromException(const sw::redis::Error& error, int attempts) {
  return ErrorStatus(RedisErrorCode::kUnknown, error.what(), attempts);
}

optional<double> ParseDouble(string_view value) {
  double parsed = 0.0;
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, error] = ::std::from_chars(first, last, parsed);
  if (error != ::std::errc() || ptr != last) {
    return ::std::nullopt;
  }
  return parsed;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// RedisClient::RetryConfig
////////////////////////////////////////////////////////////////////////////////

RedisClient::RetryConfig::RetryConfig()
    : max_attempts(kDefaultRetryMaxAttempts),
      delay(milliseconds(kDefaultRetryDelayMs)) {}

////////////////////////////////////////////////////////////////////////////////
// RedisClient::Config
////////////////////////////////////////////////////////////////////////////////

RedisClient::Config::Config()
    : host("localhost"),
      port(kDefaultPort),
      db(0),
      keep_alive(true),
      connect_timeout(milliseconds(kDefaultConnectTimeoutMs)),
      socket_timeout(milliseconds(kDefaultSocketTimeoutMs)),
      pool_size(kDefaultPoolSize),
      pool_wait_timeout(milliseconds(kDefaultPoolWaitTimeoutMs)),
      pool_connection_lifetime(milliseconds(0)),
      pool_connection_idle_time(milliseconds(0)) {}

////////////////////////////////////////////////////////////////////////////////
// RedisClient::Impl
////////////////////////////////////////////////////////////////////////////////

class RedisClient::Impl {
 public:
  explicit Impl(const RedisClient::Config& config)
      : redis_(BuildConnectionOptions(config),
               BuildConnectionPoolOptions(config)) {}

  sw::redis::Redis& redis() { return redis_; }

 private:
  sw::redis::Redis redis_;
};

////////////////////////////////////////////////////////////////////////////////
// RedisClient
////////////////////////////////////////////////////////////////////////////////

RedisClient::RedisClient(Config config) : config_(::std::move(config)) {
  ValidateRedisConfig(config_);
  impl_ = make_shared<Impl>(config_);
}

RedisStatus RedisClient::Ping() const {
  RedisStatus last_status;
  for (int attempt = 1; attempt <= config_.retry.max_attempts; ++attempt) {
    try {
      impl_->redis().ping();
      return OkStatus(attempt);
    } catch (const sw::redis::TimeoutError& error) {
      last_status = StatusFromException(error, attempt);
    } catch (const sw::redis::ClosedError& error) {
      last_status = StatusFromException(error, attempt);
    } catch (const sw::redis::IoError& error) {
      last_status = StatusFromException(error, attempt);
    } catch (const sw::redis::ReplyError& error) {
      return StatusFromException(error, attempt);
    } catch (const sw::redis::ProtoError& error) {
      return StatusFromException(error, attempt);
    } catch (const sw::redis::Error& error) {
      return StatusFromException(error, attempt);
    }

    if (attempt < config_.retry.max_attempts &&
        IsRetryable(last_status.error_code) &&
        config_.retry.delay.count() > 0) {
      ::std::this_thread::sleep_for(config_.retry.delay);
    }
  }
  return last_status;
}

RedisStringResult RedisClient::Get(string key) const {
  RedisStringResult result;
  for (int attempt = 1; attempt <= config_.retry.max_attempts; ++attempt) {
    try {
      result.value = impl_->redis().get(key);
      result.status = OkStatus(attempt);
      return result;
    } catch (const sw::redis::TimeoutError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::ClosedError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::IoError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::ReplyError& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    } catch (const sw::redis::ProtoError& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    } catch (const sw::redis::Error& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    }

    if (attempt < config_.retry.max_attempts &&
        IsRetryable(result.status.error_code) &&
        config_.retry.delay.count() > 0) {
      ::std::this_thread::sleep_for(config_.retry.delay);
    }
  }
  return result;
}

RedisStringListResult RedisClient::MGet(const vector<string>& keys) const {
  RedisStringListResult result;
  if (keys.empty()) {
    result.status = OkStatus(0);
    return result;
  }

  for (int attempt = 1; attempt <= config_.retry.max_attempts; ++attempt) {
    try {
      result.values.clear();
      impl_->redis().mget(keys.begin(), keys.end(),
                          ::std::back_inserter(result.values));
      result.status = OkStatus(attempt);
      return result;
    } catch (const sw::redis::TimeoutError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::ClosedError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::IoError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::ReplyError& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    } catch (const sw::redis::ProtoError& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    } catch (const sw::redis::Error& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    }

    if (attempt < config_.retry.max_attempts &&
        IsRetryable(result.status.error_code) &&
        config_.retry.delay.count() > 0) {
      ::std::this_thread::sleep_for(config_.retry.delay);
    }
  }
  return result;
}

RedisScoredMemberListResult RedisClient::ZRevRangeWithScores(string key,
                                                             long long start,
                                                             long long stop) const {
  RedisScoredMemberListResult result;
  for (int attempt = 1; attempt <= config_.retry.max_attempts; ++attempt) {
    try {
      vector<string> raw_values;
      impl_->redis().command("zrevrange", key, start, stop, "withscores",
                             ::std::back_inserter(raw_values));
      if (raw_values.size() % 2 != 0) {
        result.status = ErrorStatus(
            RedisErrorCode::kProtocolError,
            "Redis ZREVRANGE WITHSCORES returned an odd number of values",
            attempt);
        return result;
      }

      result.values.clear();
      result.values.reserve(raw_values.size() / 2);
      for (::std::size_t i = 0; i < raw_values.size(); i += 2) {
        optional<double> score = ParseDouble(raw_values[i + 1]);
        if (!score.has_value()) {
          result.status = ErrorStatus(
              RedisErrorCode::kProtocolError,
              "Redis ZREVRANGE WITHSCORES returned an invalid score",
              attempt);
          return result;
        }
        result.values.push_back(RedisScoredMember{
            .member = ::std::move(raw_values[i]),
            .score = *score,
        });
      }
      result.status = OkStatus(attempt);
      return result;
    } catch (const sw::redis::TimeoutError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::ClosedError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::IoError& error) {
      result.status = StatusFromException(error, attempt);
    } catch (const sw::redis::ReplyError& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    } catch (const sw::redis::ProtoError& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    } catch (const sw::redis::Error& error) {
      result.status = StatusFromException(error, attempt);
      return result;
    }

    if (attempt < config_.retry.max_attempts &&
        IsRetryable(result.status.error_code) &&
        config_.retry.delay.count() > 0) {
      ::std::this_thread::sleep_for(config_.retry.delay);
    }
  }
  return result;
}

}  // namespace utilities
}  // namespace shooting_star
