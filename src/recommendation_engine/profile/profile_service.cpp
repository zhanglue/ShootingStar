#include "src/recommendation_engine/profile/profile_service.h"

#include <chrono>
#include <cstdlib>
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

namespace recommendation_engine {

using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;
using ::shooting_star::utilities::ElasticsearchClient;
using ::std::format;
using ::std::invalid_argument;
using ::std::make_unique;
using ::std::optional;
using ::std::shared_ptr;
using ::std::string;
using ::std::string_view;
using ::std::unique_ptr;
using ::std::chrono::milliseconds;
using ::std::chrono::seconds;

namespace {

constexpr string_view kStoreTypeConfigKey = "store_type";
constexpr string_view kDataPathConfigKey = "data_path";
constexpr string_view kCacheCapacityConfigKey = "cache.capacity";
constexpr string_view kCacheTtlSecondsConfigKey = "cache.ttl_seconds";
constexpr string_view kEsBaseUrlConfigKey = "elasticsearch.base_url";
constexpr string_view kEsIndexConfigKey = "elasticsearch.index";
constexpr string_view kEsUsernameConfigKey = "elasticsearch.username";
constexpr string_view kEsPasswordConfigKey = "elasticsearch.password";
constexpr string_view kEsPasswordEnvConfigKey = "elasticsearch.password_env";
constexpr string_view kEsCaCertPathConfigKey = "elasticsearch.ca_cert_path";
constexpr string_view kEsVerifySslConfigKey = "elasticsearch.verify_ssl";
constexpr string_view kEsRequestTimeoutMsConfigKey =
    "elasticsearch.request_timeout_ms";
constexpr string_view kDefaultEsUsername = "elastic";
constexpr bool kDefaultEsVerifySsl = true;
constexpr int kDefaultEsRequestTimeoutMs = 5000;
constexpr int kDefaultCacheCapacity = 30;
constexpr int kDefaultCacheTtlSeconds = 300;
constexpr string_view kLocalStoreType = "local";
constexpr string_view kElasticsearchStoreType = "elasticsearch";

shared_ptr<const ::shooting_star::utilities::Logger> RequireLogger(
    shared_ptr<const ::shooting_star::utilities::Logger> logger) {
  if (logger == nullptr) {
    throw invalid_argument("ProfileServiceImpl logger must not be null");
  }
  return logger;
}

string GetEnvOrDefault(const string& name, string default_value) {
  if (name.empty()) {
    return default_value;
  }
  const char* value = ::std::getenv(name.c_str());
  if (value == nullptr || string(value).empty()) {
    return default_value;
  }
  return value;
}

ElasticsearchClient::Config CreateElasticsearchConfig(
    const ::shooting_star::utilities::ConfigHelper& config) {
  ElasticsearchClient::Config es_config;
  es_config.base_url = config.GetString(kEsBaseUrlConfigKey);
  es_config.username =
      config.GetString(kEsUsernameConfigKey, string(kDefaultEsUsername));
  es_config.password = config.GetString(kEsPasswordConfigKey);
  es_config.password = GetEnvOrDefault(
      config.GetString(kEsPasswordEnvConfigKey), es_config.password);
  es_config.http_config.ca_cert_path =
      config.GetString(kEsCaCertPathConfigKey);
  es_config.http_config.verify_ssl =
      config.GetBool(kEsVerifySslConfigKey, kDefaultEsVerifySsl);
  es_config.request_timeout =
      milliseconds(config.GetInt(kEsRequestTimeoutMsConfigKey,
                                 kDefaultEsRequestTimeoutMs));
  return es_config;
}

unique_ptr<ProfileStore> WrapWithCacheIfConfigured(
    const ::shooting_star::utilities::Logger& logger,
    const ::shooting_star::utilities::ConfigHelper& config,
    unique_ptr<ProfileStore> profile_store) {
  const int capacity =
      config.GetInt(kCacheCapacityConfigKey, kDefaultCacheCapacity);
  const int ttl_seconds =
      config.GetInt(kCacheTtlSecondsConfigKey, kDefaultCacheTtlSeconds);
  if (capacity <= 0 || ttl_seconds <= 0) {
    logger.Info(
        "profile_cache_disabled",
        {
            {"profile_cache_capacity", ::std::to_string(capacity)},
            {"profile_cache_ttl_seconds", ::std::to_string(ttl_seconds)},
        });
    return profile_store;
  }

  logger.Info("profile_cache_initialized",
              {
                  {"profile_cache_capacity", ::std::to_string(capacity)},
                  {"profile_cache_ttl_seconds", ::std::to_string(ttl_seconds)},
              });
  return make_unique<CachingProfileStore>(
      ::std::move(profile_store), static_cast<::std::size_t>(capacity),
      ::std::chrono::duration_cast<milliseconds>(seconds(ttl_seconds)));
}

unique_ptr<ProfileStore> CreateUncachedProfileStore(
    const ::shooting_star::utilities::Logger& logger,
    const ::shooting_star::utilities::ConfigHelper& config,
    const string& profile_store_type) {
  if (profile_store_type == kLocalStoreType) {
    const string profile_data_path = config.GetString(kDataPathConfigKey);
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
            {"profile_es_index", config.GetString(kEsIndexConfigKey)},
        });
    return make_unique<ElasticsearchProfileStore>(
        ElasticsearchClient::Create(::std::move(es_config)),
        config.GetString(kEsIndexConfigKey));
  }

  throw invalid_argument(::absl::StrFormat("Unsupported profile store type: %s",
                                           profile_store_type));
}

unique_ptr<ProfileStore> CreateProfileStore(
    const ::shooting_star::utilities::Logger& logger,
    const ::shooting_star::utilities::ConfigHelper& config,
    const string& profile_store_type) {
  return WrapWithCacheIfConfigured(
      logger, config,
      CreateUncachedProfileStore(logger, config, profile_store_type));
}

}  // namespace

ProfileServiceImpl::ProfileServiceImpl(
    ::shooting_star::utilities::YamlConfigHelper config,
    ::std::shared_ptr<const ::shooting_star::utilities::Logger> logger)
    : config_(::std::move(config)),
      logger_owner_(RequireLogger(::std::move(logger))),
      logger_(*logger_owner_) {
  const string profile_store_type =
      config_.GetString(kStoreTypeConfigKey);

  logger_.Info("profile_store_selected",
               {
                   {"profile_store_type", profile_store_type},
               });

  profile_store_ = CreateProfileStore(logger_, config_, profile_store_type);
}

Status ProfileServiceImpl::GetProfile(ServerContext* context,
                                      const GetProfileRequest* request,
                                      GetProfileResponse* response) {
  (void)context;

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
    return Status(StatusCode::INTERNAL, ex.what());
  }
  if (!profile.has_value()) {
    response->set_status(ProfileServiceStatus::PROFILE_USER_NOT_FOUND);
    return Status(StatusCode::NOT_FOUND,
                  format("User ID of {} not found.", request->user_id()));
  }

  response->set_status(ProfileServiceStatus::PROFILE_SUCCESS);
  response->mutable_profile()->CopyFrom(*profile);
  return Status::OK;
}

}  // namespace recommendation_engine
