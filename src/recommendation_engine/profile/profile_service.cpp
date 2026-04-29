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
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
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

namespace {

unique_ptr<ProfileStore> WrapWithLocalCacheIfConfigured(
    const ::shooting_star::utilities::GlobalConfig& config,
    unique_ptr<ProfileStore> profile_store) {
  const Logger& logger = LoggerRegistry::Get();
  const int capacity = config.GetLocalCacheCapacity();
  const int ttl_seconds = config.GetLocalCacheTtlSeconds();
  if (capacity <= 0) {
    const string reason =
        string(config.GetLocalCacheCapacityKey()) + " must be greater than 0";
    logger.Info(
      "profile_local_cache_disabled",
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
    logger.Info(
      "profile_local_cache_disabled",
      {
        {"reason", reason},
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
    const ::shooting_star::utilities::GlobalConfig& config,
    const string& profile_store_type) {
  const Logger& logger = LoggerRegistry::Get();
  if (profile_store_type == ProfileStore::kLocalStoreType) {
    const string profile_data_path =
        ResolveWorkspaceRelativePath(config.GetDataPath());
    logger.Info(
      "profile_store_initialized",
      {
        {"profile_store_type", profile_store_type},
        {"profile_data_path", profile_data_path},
      });
    return make_unique<LocalFileProfileStore>(profile_data_path);
  }

  if (profile_store_type == ProfileStore::kElasticsearchStoreType) {
    logger.Info(
      "profile_store_initialized",
      {
        {"profile_store_type", profile_store_type},
        {"profile_es_base_url", config.GetElasticsearchBaseUrl()},
        {"profile_es_index", config.GetElasticsearchIndex()},
      });
    return make_unique<ElasticsearchProfileStore>(
        ElasticsearchClient::Create(),
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

}  // namespace

ProfileServiceImpl::ProfileServiceImpl(
    const ::shooting_star::utilities::GlobalConfig& config)
    : config_(config) {
  const Logger& logger = LoggerRegistry::Get();
  const string profile_store_type = config_.GetStoreType();

  logger.Info(
    "profile_store_selected",
    {
      {"profile_store_type", profile_store_type},
    });

  profile_store_ = CreateProfileStore(config_, profile_store_type);
}

Status ProfileServiceImpl::GetProfile(ServerContext* /*context*/,
                                      const GetProfileRequest* request,
                                      GetProfileResponse* response) {
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

  if (!profile.has_value()) {
    response->set_status(ProfileServiceStatus::PROFILE_USER_NOT_FOUND);
    logger.Info(
      "get_profile_user_not_found",
      {
        {"user_id", to_string(request->user_id())},
      });
    return Status(
        StatusCode::NOT_FOUND,
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
