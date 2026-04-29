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
using ::shooting_star::utilities::CheckGrpcServerDeadline;
using ::shooting_star::utilities::ElasticsearchClient;
using ::shooting_star::utilities::GetEnvOrDefault;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::shooting_star::utilities::RpcDeadlineStatus;
using ::shooting_star::utilities::ValidateTimeoutNotGreater;
using ::shooting_star::utilities::ValidateTimeoutSumNotGreater;
using ::std::format;
using ::std::invalid_argument;
using ::std::make_unique;
using ::std::optional;
using ::std::string;
using ::std::string_view;
using ::std::to_string;
using ::std::unique_ptr;
using ::std::chrono::milliseconds;
using ::std::chrono::seconds;
using ::std::chrono::steady_clock;

namespace {

constexpr string_view kLocalStoreType = "local";
constexpr string_view kElasticsearchStoreType = "elasticsearch";

ElasticsearchClient::Config CreateElasticsearchConfig(
    const ::shooting_star::utilities::GlobalConfig& config) {
  ElasticsearchClient::Config es_config;
  es_config.base_url = config.GetElasticsearchBaseUrl();
  es_config.username = config.GetElasticsearchUsername();
  es_config.password = config.GetElasticsearchPassword();
  es_config.password =
      GetEnvOrDefault(config.GetElasticsearchPasswordEnv(), es_config.password);
  es_config.request_timeout =
      milliseconds(config.GetElasticsearchRequestTimeoutMs());
  es_config.http_config.pool_size =
      static_cast<::std::size_t>(config.GetElasticsearchHttpClientPoolSize());
  es_config.http_config.acquire_timeout =
      milliseconds(config.GetElasticsearchHttpClientAcquireTimeoutMs());
  es_config.http_config.request_timeout =
      milliseconds(config.GetElasticsearchHttpClientRequestTimeoutMs());
  es_config.http_config.connect_timeout =
      milliseconds(config.GetElasticsearchHttpClientConnectTimeoutMs());
  es_config.http_config.acquire_retry.max_attempts =
      config.GetElasticsearchHttpClientAcquireRetryMaxAttempts();
  es_config.http_config.acquire_retry.delay =
      milliseconds(config.GetElasticsearchHttpClientAcquireRetryDelayMs());
  es_config.http_config.connect_retry.max_attempts =
      config.GetElasticsearchHttpClientConnectRetryMaxAttempts();
  es_config.http_config.connect_retry.delay =
      milliseconds(config.GetElasticsearchHttpClientConnectRetryDelayMs());
  es_config.http_config.request_retry.max_attempts =
      config.GetElasticsearchHttpClientRequestRetryMaxAttempts();
  es_config.http_config.request_retry.delay =
      milliseconds(config.GetElasticsearchHttpClientRequestRetryDelayMs());
  ValidateTimeoutNotGreater(
      config.GetElasticsearchHttpClientAcquireTimeoutMsKey(),
      es_config.http_config.acquire_timeout,
      config.GetElasticsearchRequestTimeoutMsKey(), *es_config.request_timeout);
  ValidateTimeoutNotGreater(
      config.GetElasticsearchHttpClientRequestTimeoutMsKey(),
      es_config.http_config.request_timeout,
      config.GetElasticsearchRequestTimeoutMsKey(), *es_config.request_timeout);
  ValidateTimeoutNotGreater(
      config.GetElasticsearchHttpClientConnectTimeoutMsKey(),
      es_config.http_config.connect_timeout,
      config.GetElasticsearchHttpClientRequestTimeoutMsKey(),
      es_config.http_config.request_timeout);
  ValidateTimeoutSumNotGreater(
      config.GetElasticsearchHttpClientAcquireTimeoutMsKey(),
      es_config.http_config.acquire_timeout,
      config.GetElasticsearchHttpClientRequestTimeoutMsKey(),
      es_config.http_config.request_timeout,
      config.GetElasticsearchRequestTimeoutMsKey(), *es_config.request_timeout);
  ValidateTimeoutNotGreater(config.GetElasticsearchRequestTimeoutMsKey(),
                            *es_config.request_timeout,
                            config.GetGetProfileTimeoutMsKey(),
                            milliseconds(config.GetGetProfileTimeoutMs()));
  es_config.http_config.follow_redirects =
      config.GetElasticsearchHttpClientFollowRedirects();
  es_config.http_config.verify_ssl =
      config.GetElasticsearchHttpClientVerifySsl();
  es_config.http_config.ca_cert_path =
      config.GetElasticsearchHttpClientCaCertPath();
  return es_config;
}

unique_ptr<ProfileStore> WrapWithLocalCacheIfConfigured(
    const ::shooting_star::utilities::GlobalConfig& config,
    unique_ptr<ProfileStore> profile_store) {
  const Logger& logger = LoggerRegistry::Get();
  const int capacity = config.GetLocalCacheCapacity();
  const int ttl_seconds = config.GetLocalCacheTtlSeconds();
  if (capacity <= 0) {
    const string reason =
        string(config.GetLocalCacheCapacityKey()) + " must be greater than 0";
    logger.Info("profile_local_cache_disabled",
                {
                    {"reason", reason},
                    {"profile_local_cache_capacity", to_string(capacity)},
                    {"profile_local_cache_ttl_seconds", to_string(ttl_seconds)},
                });
    return profile_store;
  }
  if (ttl_seconds <= 0) {
    const string reason =
        string(config.GetLocalCacheTtlSecondsKey()) + " must be greater than 0";
    logger.Info("profile_local_cache_disabled",
                {
                    {"reason", reason},
                    {"profile_local_cache_capacity", to_string(capacity)},
                    {"profile_local_cache_ttl_seconds", to_string(ttl_seconds)},
                });
    return profile_store;
  }

  logger.Info("profile_local_cache_initialized",
              {
                  {"profile_local_cache_capacity", to_string(capacity)},
                  {"profile_local_cache_ttl_seconds", to_string(ttl_seconds)},
              });
  return make_unique<CachingProfileStore>(
      ::std::move(profile_store), static_cast<::std::size_t>(capacity),
      ::std::chrono::duration_cast<milliseconds>(seconds(ttl_seconds)));
}

unique_ptr<ProfileStore> CreateUncachedProfileStore(
    const ::shooting_star::utilities::GlobalConfig& config,
    const string& profile_store_type) {
  const Logger& logger = LoggerRegistry::Get();
  if (profile_store_type == kLocalStoreType) {
    const string profile_data_path =
        ResolveWorkspaceRelativePath(config.GetDataPath());
    logger.Info("profile_store_initialized",
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
            {"profile_es_index", config.GetElasticsearchIndex()},
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
        config.GetElasticsearchIndex());
  }

  throw invalid_argument(::absl::StrFormat("Unsupported profile store type: %s",
                                           profile_store_type));
}

unique_ptr<ProfileStore> CreateProfileStore(
    const ::shooting_star::utilities::GlobalConfig& config,
    const string& profile_store_type) {
  return WrapWithLocalCacheIfConfigured(
      config, CreateUncachedProfileStore(config, profile_store_type));
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
    const ::shooting_star::utilities::GlobalConfig& config)
    : config_(config),
      get_profile_timeout_(milliseconds(config_.GetGetProfileTimeoutMs())) {
  const Logger& logger = LoggerRegistry::Get();
  const string profile_store_type = config_.GetStoreType();

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
  logger.Info("get_profile_request_received",
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
    logger.Info("get_profile_request_failed",
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
    logger.Info(GetProfileDeadlineEvent(deadline_status),
                {
                    {"user_id", to_string(request->user_id())},
                    {"reason", reason},
                });
    return Status(StatusCode::DEADLINE_EXCEEDED, reason);
  }

  if (!profile.has_value()) {
    response->set_status(ProfileServiceStatus::PROFILE_USER_NOT_FOUND);
    logger.Info("get_profile_user_not_found",
                {
                    {"user_id", to_string(request->user_id())},
                });
    return Status(StatusCode::NOT_FOUND,
                  format("User ID of {} not found.", request->user_id()));
  }

  logger.Info("get_profile_request_succeeded",
              {
                  {"user_id", to_string(request->user_id())},
              });
  logger.Debug("profile_payload",
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
