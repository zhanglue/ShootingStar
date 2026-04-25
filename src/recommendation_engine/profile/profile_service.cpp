#include "src/recommendation_engine/profile/profile_service.h"

#include <chrono>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_format.h"
#include "src/recommendation_engine/profile/caching_profile_store.h"
#include "src/recommendation_engine/profile/elasticsearch_profile_store.h"
#include "src/recommendation_engine/profile/local_file_profile_store.h"
#include "src/utilities/elasticsearch_client/elasticsearch_client.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace recommendation_engine {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::shooting_star::utilities::ElasticsearchClient;
using ::std::format;
using ::std::invalid_argument;
using ::std::make_unique;
using ::std::optional;
using ::std::string;
using ::std::string_view;
using ::std::to_string;
using ::std::unique_ptr;
using ::std::chrono::milliseconds;
using ::std::chrono::steady_clock;
using ::std::chrono::seconds;
using ::shooting_star::utilities::GetEnvOrDefault;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::CheckGrpcServerDeadline;
using ::shooting_star::utilities::RpcDeadlineStatus;
using ::shooting_star::utilities::ValidateTimeoutNotGreater;
using ::shooting_star::utilities::ValidateTimeoutSumNotGreater;

namespace {

constexpr string_view kStoreTypeConfigKey = "store_type";
constexpr string_view kDataPathConfigKey = "data_path";
constexpr string_view kGetProfileTimeoutMsConfigKey =
    "server.get_profile_timeout_ms";
constexpr string_view kLocalCacheCapacityConfigKey = "local_cache.capacity";
constexpr string_view kLocalCacheTtlSecondsConfigKey =
    "local_cache.ttl_seconds";
constexpr string_view kEsBaseUrlConfigKey = "elasticsearch.base_url";
constexpr string_view kEsIndexConfigKey = "elasticsearch.index";
constexpr string_view kEsUsernameConfigKey = "elasticsearch.username";
constexpr string_view kEsPasswordConfigKey = "elasticsearch.password";
constexpr string_view kEsPasswordEnvConfigKey = "elasticsearch.password_env";
constexpr string_view kEsRequestTimeoutMsConfigKey =
    "elasticsearch.request_timeout_ms";
constexpr string_view kEsHttpClientPoolSizeConfigKey =
    "elasticsearch.http_client.curl_handle_pool.pool_size";
constexpr string_view kEsHttpClientAcquireTimeoutMsConfigKey =
    "elasticsearch.http_client.curl_handle_pool.acquire_timeout_ms";
constexpr string_view kEsHttpClientRequestTimeoutMsConfigKey =
    "elasticsearch.http_client.request_timeout_ms";
constexpr string_view kEsHttpClientConnectTimeoutMsConfigKey =
    "elasticsearch.http_client.connect_timeout_ms";
constexpr string_view kEsHttpClientAcquireRetryMaxAttemptsConfigKey =
    "elasticsearch.http_client.curl_handle_pool.retry.max_attempts";
constexpr string_view kEsHttpClientAcquireRetryDelayMsConfigKey =
    "elasticsearch.http_client.curl_handle_pool.retry.delay_ms";
constexpr string_view kEsHttpClientConnectRetryMaxAttemptsConfigKey =
    "elasticsearch.http_client.connect_retry.max_attempts";
constexpr string_view kEsHttpClientConnectRetryDelayMsConfigKey =
    "elasticsearch.http_client.connect_retry.delay_ms";
constexpr string_view kEsHttpClientRequestRetryMaxAttemptsConfigKey =
    "elasticsearch.http_client.request_retry.max_attempts";
constexpr string_view kEsHttpClientRequestRetryDelayMsConfigKey =
    "elasticsearch.http_client.request_retry.delay_ms";
constexpr string_view kEsHttpClientFollowRedirectsConfigKey =
    "elasticsearch.http_client.follow_redirects";
constexpr string_view kEsHttpClientVerifySslConfigKey =
    "elasticsearch.http_client.verify_ssl";
constexpr string_view kEsHttpClientCaCertPathConfigKey =
    "elasticsearch.http_client.ca_cert_path";
constexpr string_view kDefaultEsUsername = "elastic";
constexpr int kDefaultEsRequestTimeoutMs = 100;
constexpr int kDefaultEsHttpClientPoolSize = 4;
constexpr int kDefaultEsHttpClientAcquireTimeoutMs = 30;
constexpr int kDefaultEsHttpClientRequestTimeoutMs = 30;
constexpr int kDefaultEsHttpClientConnectTimeoutMs = 20;
constexpr int kDefaultEsHttpClientRetryMaxAttempts = 3;
constexpr int kDefaultEsHttpClientRetryDelayMs = 0;
constexpr int kDefaultGetProfileTimeoutMs = 120;
constexpr bool kDefaultEsHttpClientFollowRedirects = true;
constexpr bool kDefaultEsHttpClientVerifySsl = true;
constexpr int kDefaultCacheCapacity = 30;
constexpr int kDefaultCacheTtlSeconds = 300;
constexpr string_view kLocalStoreType = "local";
constexpr string_view kElasticsearchStoreType = "elasticsearch";

ElasticsearchClient::Config CreateElasticsearchConfig(
    const ::shooting_star::utilities::ConfigHelper& config) {
  ElasticsearchClient::Config es_config;
  es_config.base_url = config.GetString(kEsBaseUrlConfigKey);
  es_config.username =
      config.GetString(kEsUsernameConfigKey, string(kDefaultEsUsername));
  es_config.password = config.GetString(kEsPasswordConfigKey);
  es_config.password = GetEnvOrDefault(
      config.GetString(kEsPasswordEnvConfigKey), es_config.password);
  es_config.request_timeout = milliseconds(config.GetPositiveInt(
      kEsRequestTimeoutMsConfigKey, kDefaultEsRequestTimeoutMs));
  es_config.http_config.pool_size = static_cast<::std::size_t>(
      config.GetPositiveInt(kEsHttpClientPoolSizeConfigKey,
                            kDefaultEsHttpClientPoolSize));
  es_config.http_config.acquire_timeout = milliseconds(config.GetPositiveInt(
      kEsHttpClientAcquireTimeoutMsConfigKey,
      kDefaultEsHttpClientAcquireTimeoutMs));
  es_config.http_config.request_timeout = milliseconds(config.GetPositiveInt(
      kEsHttpClientRequestTimeoutMsConfigKey,
      kDefaultEsHttpClientRequestTimeoutMs));
  es_config.http_config.connect_timeout = milliseconds(config.GetPositiveInt(
      kEsHttpClientConnectTimeoutMsConfigKey,
      kDefaultEsHttpClientConnectTimeoutMs));
  es_config.http_config.acquire_retry.max_attempts = config.GetPositiveInt(
      kEsHttpClientAcquireRetryMaxAttemptsConfigKey,
      kDefaultEsHttpClientRetryMaxAttempts);
  es_config.http_config.acquire_retry.delay = milliseconds(
      config.GetNonNegativeInt(
          kEsHttpClientAcquireRetryDelayMsConfigKey,
          kDefaultEsHttpClientRetryDelayMs));
  es_config.http_config.connect_retry.max_attempts = config.GetPositiveInt(
      kEsHttpClientConnectRetryMaxAttemptsConfigKey,
      kDefaultEsHttpClientRetryMaxAttempts);
  es_config.http_config.connect_retry.delay = milliseconds(
      config.GetNonNegativeInt(
          kEsHttpClientConnectRetryDelayMsConfigKey,
          kDefaultEsHttpClientRetryDelayMs));
  es_config.http_config.request_retry.max_attempts = config.GetPositiveInt(
      kEsHttpClientRequestRetryMaxAttemptsConfigKey,
      kDefaultEsHttpClientRetryMaxAttempts);
  es_config.http_config.request_retry.delay = milliseconds(
      config.GetNonNegativeInt(
          kEsHttpClientRequestRetryDelayMsConfigKey,
          kDefaultEsHttpClientRetryDelayMs));
  ValidateTimeoutNotGreater(kEsHttpClientAcquireTimeoutMsConfigKey,
                            es_config.http_config.acquire_timeout,
                            kEsRequestTimeoutMsConfigKey,
                            *es_config.request_timeout);
  ValidateTimeoutNotGreater(kEsHttpClientRequestTimeoutMsConfigKey,
                            es_config.http_config.request_timeout,
                            kEsRequestTimeoutMsConfigKey,
                            *es_config.request_timeout);
  ValidateTimeoutNotGreater(kEsHttpClientConnectTimeoutMsConfigKey,
                            es_config.http_config.connect_timeout,
                            kEsHttpClientRequestTimeoutMsConfigKey,
                            es_config.http_config.request_timeout);
  ValidateTimeoutSumNotGreater(kEsHttpClientAcquireTimeoutMsConfigKey,
                               es_config.http_config.acquire_timeout,
                               kEsHttpClientRequestTimeoutMsConfigKey,
                               es_config.http_config.request_timeout,
                               kEsRequestTimeoutMsConfigKey,
                               *es_config.request_timeout);
  ValidateTimeoutNotGreater(
      kEsRequestTimeoutMsConfigKey,
      *es_config.request_timeout,
      kGetProfileTimeoutMsConfigKey,
      milliseconds(config.GetPositiveInt(kGetProfileTimeoutMsConfigKey,
                                          kDefaultGetProfileTimeoutMs)));
  es_config.http_config.follow_redirects =
      config.GetBool(kEsHttpClientFollowRedirectsConfigKey,
                     kDefaultEsHttpClientFollowRedirects);
  es_config.http_config.verify_ssl =
      config.GetBool(kEsHttpClientVerifySslConfigKey,
                     kDefaultEsHttpClientVerifySsl);
  es_config.http_config.ca_cert_path =
      config.GetString(kEsHttpClientCaCertPathConfigKey);
  return es_config;
}

unique_ptr<ProfileStore> WrapWithLocalCacheIfConfigured(
    const ::shooting_star::utilities::ConfigHelper& config,
    unique_ptr<ProfileStore> profile_store) {
  const Logger& logger = LoggerRegistry::Get();
  if (!config.Has(kLocalCacheCapacityConfigKey)) {
    logger.Info(
        "profile_local_cache_disabled",
        {
            {"reason", "local_cache.capacity is not configured"},
        });
    return profile_store;
  }

  const int capacity =
      config.GetInt(kLocalCacheCapacityConfigKey, kDefaultCacheCapacity);
  const int ttl_seconds =
      config.GetInt(kLocalCacheTtlSecondsConfigKey, kDefaultCacheTtlSeconds);
  if (capacity <= 0) {
    logger.Info(
        "profile_local_cache_disabled",
        {
            {"reason", "local_cache.capacity must be greater than 0"},
            {"profile_local_cache_capacity", to_string(capacity)},
            {"profile_local_cache_ttl_seconds", to_string(ttl_seconds)},
        });
    return profile_store;
  }
  if (ttl_seconds <= 0) {
    logger.Info(
        "profile_local_cache_disabled",
        {
            {"reason", "local_cache.ttl_seconds must be greater than 0"},
            {"profile_local_cache_capacity", to_string(capacity)},
            {"profile_local_cache_ttl_seconds", to_string(ttl_seconds)},
        });
    return profile_store;
  }

  logger.Info(
      "profile_local_cache_initialized",
      {
          {"profile_local_cache_capacity", to_string(capacity)},
          {"profile_local_cache_ttl_seconds", to_string(ttl_seconds)},
      });
  return make_unique<CachingProfileStore>(
      ::std::move(profile_store), static_cast<::std::size_t>(capacity),
      ::std::chrono::duration_cast<milliseconds>(seconds(ttl_seconds)));
}

unique_ptr<ProfileStore> CreateUncachedProfileStore(
    const ::shooting_star::utilities::ConfigHelper& config,
    const string& profile_store_type) {
  const Logger& logger = LoggerRegistry::Get();
  if (profile_store_type == kLocalStoreType) {
    const string profile_data_path = config.GetString(kDataPathConfigKey);
    logger.Info(
        "profile_store_initialized",
        {
            {"profile_store_type", profile_store_type},
            {"profile_data_path", profile_data_path},
        });
    return make_unique<LocalFileProfileStore>(profile_data_path);
  }

  if (profile_store_type == kElasticsearchStoreType) {
    ElasticsearchClient::Config es_config = CreateElasticsearchConfig(config);
    logger.Info(
        "profile_store_initialized",
        {
            {"profile_store_type", profile_store_type},
            {"profile_es_base_url", es_config.base_url},
            {"profile_es_index", config.GetString(kEsIndexConfigKey)},
            {"profile_es_request_timeout_ms",
             to_string(es_config.request_timeout->count())},
            {"profile_es_http_client_curl_handle_pool_size",
             to_string(es_config.http_config.pool_size)},
            {"profile_es_http_client_curl_handle_pool_acquire_timeout_ms",
             to_string(es_config.http_config.acquire_timeout.count())},
            {"profile_es_http_client_request_timeout_ms",
             to_string(es_config.http_config.request_timeout.count())},
            {"profile_es_http_client_connect_timeout_ms",
             to_string(es_config.http_config.connect_timeout.count())},
            {"profile_es_http_client_curl_handle_pool_retry_max_attempts",
             to_string(es_config.http_config.acquire_retry.max_attempts)},
            {"profile_es_http_client_curl_handle_pool_retry_delay_ms",
             to_string(es_config.http_config.acquire_retry.delay.count())},
            {"profile_es_http_client_connect_retry_max_attempts",
             to_string(es_config.http_config.connect_retry.max_attempts)},
            {"profile_es_http_client_connect_retry_delay_ms",
             to_string(es_config.http_config.connect_retry.delay.count())},
            {"profile_es_http_client_request_retry_max_attempts",
             to_string(es_config.http_config.request_retry.max_attempts)},
            {"profile_es_http_client_request_retry_delay_ms",
             to_string(es_config.http_config.request_retry.delay.count())},
            {"profile_es_http_client_follow_redirects",
             es_config.http_config.follow_redirects ? "true" : "false"},
            {"profile_es_http_client_verify_ssl",
             es_config.http_config.verify_ssl ? "true" : "false"},
            {"profile_es_http_client_ca_cert_path",
             es_config.http_config.ca_cert_path},
        });
    return make_unique<ElasticsearchProfileStore>(
        ElasticsearchClient::Create(::std::move(es_config)),
        config.GetString(kEsIndexConfigKey));
  }

  throw invalid_argument(::absl::StrFormat("Unsupported profile store type: %s",
                                           profile_store_type));
}

unique_ptr<ProfileStore> CreateProfileStore(
    const ::shooting_star::utilities::ConfigHelper& config,
    const string& profile_store_type) {
  return WrapWithLocalCacheIfConfigured(
      config,
      CreateUncachedProfileStore(config, profile_store_type));
}

string GetProfileDeadlineReason(RpcDeadlineStatus deadline_status) {
  switch (deadline_status) {
    case RpcDeadlineStatus::kCancelled:
      return "GetProfile request was cancelled.";
    case RpcDeadlineStatus::kClientDeadlineExceeded:
      return "GetProfile client deadline exceeded.";
    case RpcDeadlineStatus::kServerDeadlineExceeded:
      return "GetProfile service timeout exceeded.";
    case RpcDeadlineStatus::kOk:
      return "";
  }
  return "";
}

string_view GetProfileDeadlineEvent(RpcDeadlineStatus deadline_status) {
  switch (deadline_status) {
    case RpcDeadlineStatus::kCancelled:
      return "get_profile_request_cancelled";
    case RpcDeadlineStatus::kClientDeadlineExceeded:
      return "get_profile_client_deadline_exceeded";
    case RpcDeadlineStatus::kServerDeadlineExceeded:
      return "get_profile_service_timeout_exceeded";
    case RpcDeadlineStatus::kOk:
      return "";
  }
  return "";
}

}  // namespace

ProfileServiceImpl::ProfileServiceImpl(
    ::shooting_star::utilities::YamlConfigHelper config)
    : config_(::std::move(config)),
      get_profile_timeout_(milliseconds(config_.GetPositiveInt(
          kGetProfileTimeoutMsConfigKey,
          kDefaultGetProfileTimeoutMs))) {
  const Logger& logger = LoggerRegistry::Get();
  const string profile_store_type =
      config_.GetString(kStoreTypeConfigKey);

  logger.Info(
      "profile_store_selected",
      {
          {"profile_store_type", profile_store_type},
          {"get_profile_timeout_ms", to_string(get_profile_timeout_.count())},
      });

  profile_store_ = CreateProfileStore(config_, profile_store_type);
}

Status ProfileServiceImpl::GetProfile(ServerContext* context,
                                      const GetProfileRequest* request,
                                      GetProfileResponse* response) {
  const steady_clock::time_point request_deadline =
      steady_clock::now() + get_profile_timeout_;
  const Logger& logger = LoggerRegistry::Get();
  logger.Info(
      "get_profile_request_received",
      {
          {"user_id", to_string(request->user_id())},
      });

  response->mutable_request()->CopyFrom(*request);

  if (profile_store_ == nullptr) {
    response->set_status(ProfileServiceStatus::PROFILE_SYSTEM_ERROR);
    return Status(StatusCode::INTERNAL, "Profile store is not initialized.");
  }

  optional<Profile> profile;
  try {
    profile = profile_store_->FindByUserId(request->user_id());
  } catch (const ::std::exception& ex) {
    response->set_status(ProfileServiceStatus::PROFILE_SYSTEM_ERROR);
    logger.Info(
        "get_profile_request_failed",
        {
            {"user_id", to_string(request->user_id())},
            {"reason", "profile store lookup failed"},
            {"error_message", ex.what()},
        });
    return Status(StatusCode::INTERNAL, ex.what());
  }

  RpcDeadlineStatus deadline_status =
      CheckGrpcServerDeadline(context, request_deadline);
  if (deadline_status != RpcDeadlineStatus::kOk) {
    const string reason = GetProfileDeadlineReason(deadline_status);
    response->set_status(ProfileServiceStatus::PROFILE_SYSTEM_ERROR);
    logger.Info(
        GetProfileDeadlineEvent(deadline_status),
        {
            {"user_id", to_string(request->user_id())},
            {"reason", reason},
        });
    return Status(StatusCode::DEADLINE_EXCEEDED, reason);
  }

  if (!profile.has_value()) {
    response->set_status(ProfileServiceStatus::PROFILE_USER_NOT_FOUND);
    logger.Info(
        "get_profile_user_not_found",
        {
            {"user_id", to_string(request->user_id())},
        });
    return Status(StatusCode::NOT_FOUND,
                  format("User ID of {} not found.", request->user_id()));
  }

  logger.Info(
      "get_profile_request_succeeded",
      {
          {"user_id", to_string(request->user_id())},
      });
  logger.Debug(
      "profile_payload",
      {
          {"user_id", to_string(request->user_id())},
          {"profile_size_bytes", to_string(profile->ByteSizeLong())},
          {"profile_proto", profile->DebugString()},
      });

  response->set_status(ProfileServiceStatus::PROFILE_SUCCESS);
  response->mutable_profile()->CopyFrom(*profile);
  return Status::OK;
}

}  // namespace recommendation_engine
