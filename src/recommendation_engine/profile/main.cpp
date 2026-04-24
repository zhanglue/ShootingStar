#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "src/recommendation_engine/profile/profile_service.h"
#include "src/utilities/config_helper/config_helper.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace {

using ::recommendation_engine::ProfileServiceImpl;
using ::shooting_star::utilities::ConfigHelper;
using ::shooting_star::utilities::LogField;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::YamlConfigHelper;
using ::std::string;
using ::std::string_view;
using ::std::vector;

constexpr string_view kServiceName = "profile";
constexpr string_view kConfigPathFlagName = "config_path";
constexpr string_view kDefaultConfigPath = "config.debug.yaml";
constexpr string_view kSourceDefaultConfigPath =
    "src/recommendation_engine/profile/config.debug.yaml";
constexpr uint16_t kDefaultServerPort = 50100;
constexpr string_view kDefaultLogLevel = "INFO";
constexpr string_view kDefaultStoreType = "local";
constexpr string_view kDefaultDataPath =
    "tests/testdata/recommendation_engine/profile/demo_profiles.json";
constexpr int kDefaultCacheCapacity = 30;
constexpr int kDefaultCacheTtlSeconds = 300;
constexpr string_view kDefaultEsBaseUrl = "";
constexpr string_view kDefaultEsIndex = "movielens_32m_user_profile";
constexpr string_view kDefaultEsUsername = "elastic";
constexpr string_view kDefaultEsPassword = "";
constexpr string_view kDefaultEsPasswordEnv = "ES_PASSWORD";
constexpr string_view kDefaultEsCaCertPath = "";
constexpr bool kDefaultEsVerifySsl = true;
constexpr int kDefaultEsRequestTimeoutMs = 5000;
constexpr string_view kServerPortConfigKey = "server.port";
constexpr string_view kServerLogLevelConfigKey = "server.log_level";
constexpr string_view kStoreTypeConfigKey = "store_type";
constexpr string_view kDataPathConfigKey = "data_path";
constexpr string_view kLocalCacheCapacityConfigKey = "local_cache.capacity";
constexpr string_view kLocalCacheTtlSecondsConfigKey =
    "local_cache.ttl_seconds";
constexpr string_view kEsBaseUrlConfigKey = "elasticsearch.base_url";
constexpr string_view kEsIndexConfigKey = "elasticsearch.index";
constexpr string_view kEsUsernameConfigKey = "elasticsearch.username";
constexpr string_view kEsPasswordConfigKey = "elasticsearch.password";
constexpr string_view kEsPasswordEnvConfigKey = "elasticsearch.password_env";
constexpr string_view kEsCaCertPathConfigKey = "elasticsearch.ca_cert_path";
constexpr string_view kEsVerifySslConfigKey = "elasticsearch.verify_ssl";
constexpr string_view kEsRequestTimeoutMsConfigKey =
    "elasticsearch.request_timeout_ms";
constexpr string_view kConfigPathConfigKey = "config.path";
constexpr string_view kListenAddressHost = "0.0.0.0";
constexpr string_view kRedactedValue = "<redacted>";

}  // namespace

ABSL_FLAG(::std::string, config_path, string(kDefaultConfigPath),
          "Path to the profile service config file.");
ABSL_FLAG(uint16_t, port, kDefaultServerPort,
          "Server port for the service");
ABSL_FLAG(::std::string, log_level, string(kDefaultLogLevel),
          "Minimum log level. Supported values: DEBUG, INFO, WARNING, ERROR.");
ABSL_FLAG(::std::string, store_type, string(kDefaultStoreType),
          "Profile store type. Supported values: local, elasticsearch.");
ABSL_FLAG(::std::string, data_path, string(kDefaultDataPath),
          "Path to the profile data JSON file.");
ABSL_FLAG(int, cache_capacity, kDefaultCacheCapacity,
          "Maximum number of profiles to keep in the local LRU cache.");
ABSL_FLAG(int, cache_ttl_seconds, kDefaultCacheTtlSeconds,
          "Local profile cache entry TTL in seconds.");
ABSL_FLAG(::std::string, es_base_url, string(kDefaultEsBaseUrl),
          "Elasticsearch base URL for profile store.");
ABSL_FLAG(::std::string, es_index, string(kDefaultEsIndex),
          "Elasticsearch profile index.");
ABSL_FLAG(::std::string, es_username, string(kDefaultEsUsername),
          "Elasticsearch username.");
ABSL_FLAG(::std::string, es_password, string(kDefaultEsPassword),
          "Elasticsearch password.");
ABSL_FLAG(::std::string, es_password_env, string(kDefaultEsPasswordEnv),
          "Environment variable containing the Elasticsearch password.");
ABSL_FLAG(::std::string, es_ca_cert_path, string(kDefaultEsCaCertPath),
          "Elasticsearch CA certificate path.");
ABSL_FLAG(bool, es_verify_ssl, kDefaultEsVerifySsl,
          "Whether to verify Elasticsearch TLS certificates.");
ABSL_FLAG(int, es_request_timeout_ms, kDefaultEsRequestTimeoutMs,
          "Elasticsearch request timeout in milliseconds.");

namespace {

struct StartupConfigPath {
  string path;
  bool from_config_path_flag = false;
};

StartupConfigPath FindStartupConfigPath(int argc, char** argv) {
  string flag_prefix("--");
  flag_prefix.append(kConfigPathFlagName);
  flag_prefix.push_back('=');
  string flag_name("--");
  flag_name.append(kConfigPathFlagName);

  for (int i = 1; i < argc; ++i) {
    const string_view arg(argv[i]);
    if (arg == flag_name && i + 1 < argc) {
      return {argv[i + 1], true};
    }
    if (arg.size() >= flag_prefix.size() &&
        arg.substr(0, flag_prefix.size()) == flag_prefix) {
      return {string(arg.substr(flag_prefix.size())), true};
    }
  }

  return {::absl::GetFlag(FLAGS_config_path), false};
}

bool HasCommandLineFlag(int argc, char** argv, string_view flag_name) {
  string flag_prefix("--");
  flag_prefix.append(flag_name);
  flag_prefix.push_back('=');
  string flag("--");
  flag.append(flag_name);

  for (int i = 1; i < argc; ++i) {
    const string_view arg(argv[i]);
    if (arg == flag) {
      return true;
    }
    if (arg.size() >= flag_prefix.size() &&
        arg.substr(0, flag_prefix.size()) == flag_prefix) {
      return true;
    }
  }

  return false;
}

string ResolveStartupConfigPath(const string& config_path,
                                const string& executable_path,
                                bool from_config_path_flag) {
  if (config_path.empty()) {
    return config_path;
  }

  const ::std::filesystem::path path(config_path);
  if (path.is_absolute()) {
    return config_path;
  }

  const ::std::filesystem::path executable_relative_path =
      (::std::filesystem::absolute(executable_path).parent_path() / path)
          .lexically_normal();
  if (from_config_path_flag || config_path != string(kDefaultConfigPath) ||
      ::std::filesystem::exists(executable_relative_path)) {
    return executable_relative_path.string();
  }

  const string source_default_config_path =
      ::shooting_star::utilities::ResolveWorkspaceRelativePath(
          string(kSourceDefaultConfigPath), executable_path);
  if (::std::filesystem::exists(source_default_config_path)) {
    return source_default_config_path;
  }

  return executable_relative_path.string();
}

YamlConfigHelper LoadConfigFile(int argc, char** argv) {
  const StartupConfigPath startup_config_path =
      FindStartupConfigPath(argc, argv);
  const string config_path =
      ResolveStartupConfigPath(startup_config_path.path, argv[0],
                               startup_config_path.from_config_path_flag);

  YamlConfigHelper config;
  config.LoadFromYamlFile(config_path);
  config.Set(string(kConfigPathConfigKey), config_path);
  return config;
}

void SeedFlagsFromConfig(const ConfigHelper& config) {
  if (config.Has(kServerPortConfigKey)) {
    ::absl::SetFlag(&FLAGS_port, config.GetUInt16(kServerPortConfigKey,
                                                  ::absl::GetFlag(FLAGS_port)));
  }
  if (config.Has(kServerLogLevelConfigKey)) {
    ::absl::SetFlag(&FLAGS_log_level,
                    config.GetString(kServerLogLevelConfigKey,
                                     ::absl::GetFlag(FLAGS_log_level)));
  }
  if (config.Has(kStoreTypeConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_store_type,
        config.GetString(kStoreTypeConfigKey,
                         ::absl::GetFlag(FLAGS_store_type)));
  }
  if (config.Has(kDataPathConfigKey)) {
    ::absl::SetFlag(&FLAGS_data_path,
                    config.GetString(kDataPathConfigKey,
                                     ::absl::GetFlag(FLAGS_data_path)));
  }
  if (config.Has(kLocalCacheCapacityConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_cache_capacity,
        config.GetInt(kLocalCacheCapacityConfigKey,
                      ::absl::GetFlag(FLAGS_cache_capacity)));
  }
  if (config.Has(kLocalCacheTtlSecondsConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_cache_ttl_seconds,
        config.GetInt(kLocalCacheTtlSecondsConfigKey,
                      ::absl::GetFlag(FLAGS_cache_ttl_seconds)));
  }
  if (config.Has(kEsBaseUrlConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_es_base_url,
        config.GetString(kEsBaseUrlConfigKey,
                         ::absl::GetFlag(FLAGS_es_base_url)));
  }
  if (config.Has(kEsIndexConfigKey)) {
    ::absl::SetFlag(&FLAGS_es_index,
                    config.GetString(kEsIndexConfigKey,
                                     ::absl::GetFlag(FLAGS_es_index)));
  }
  if (config.Has(kEsUsernameConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_es_username,
        config.GetString(kEsUsernameConfigKey,
                         ::absl::GetFlag(FLAGS_es_username)));
  }
  if (config.Has(kEsPasswordConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_es_password,
        config.GetString(kEsPasswordConfigKey,
                         ::absl::GetFlag(FLAGS_es_password)));
  }
  if (config.Has(kEsPasswordEnvConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_es_password_env,
        config.GetString(kEsPasswordEnvConfigKey,
                         ::absl::GetFlag(FLAGS_es_password_env)));
  }
  if (config.Has(kEsCaCertPathConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_es_ca_cert_path,
        config.GetString(kEsCaCertPathConfigKey,
                         ::absl::GetFlag(FLAGS_es_ca_cert_path)));
  }
  if (config.Has(kEsVerifySslConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_es_verify_ssl,
        config.GetBool(kEsVerifySslConfigKey,
                       ::absl::GetFlag(FLAGS_es_verify_ssl)));
  }
  if (config.Has(kEsRequestTimeoutMsConfigKey)) {
    ::absl::SetFlag(
        &FLAGS_es_request_timeout_ms,
        config.GetInt(kEsRequestTimeoutMsConfigKey,
                      ::absl::GetFlag(FLAGS_es_request_timeout_ms)));
  }
}

void ApplyCommandLineOverridesToConfig(int argc, char** argv,
                                       YamlConfigHelper* config) {
  const bool has_local_cache_capacity_config =
      config->Has(kLocalCacheCapacityConfigKey);
  const bool has_local_cache_ttl_config =
      config->Has(kLocalCacheTtlSecondsConfigKey);
  const bool has_cache_capacity_flag =
      HasCommandLineFlag(argc, argv, "cache_capacity");
  const bool has_cache_ttl_seconds_flag =
      HasCommandLineFlag(argc, argv, "cache_ttl_seconds");

  SeedFlagsFromConfig(*config);
  ::absl::ParseCommandLine(argc, argv);

  config->Set(string(kServerPortConfigKey),
              ::std::to_string(::absl::GetFlag(FLAGS_port)));
  config->Set(string(kServerLogLevelConfigKey),
              ::absl::GetFlag(FLAGS_log_level));
  config->Set(string(kStoreTypeConfigKey),
              ::absl::GetFlag(FLAGS_store_type));
  config->Set(string(kDataPathConfigKey),
              ::absl::GetFlag(FLAGS_data_path));
  if (has_local_cache_capacity_config || has_cache_capacity_flag) {
    config->Set(
        string(kLocalCacheCapacityConfigKey),
        ::std::to_string(::absl::GetFlag(FLAGS_cache_capacity)));
  }
  if (has_local_cache_ttl_config || has_cache_ttl_seconds_flag) {
    config->Set(
        string(kLocalCacheTtlSecondsConfigKey),
        ::std::to_string(::absl::GetFlag(FLAGS_cache_ttl_seconds)));
  }
  config->Set(string(kEsBaseUrlConfigKey),
              ::absl::GetFlag(FLAGS_es_base_url));
  config->Set(string(kEsIndexConfigKey),
              ::absl::GetFlag(FLAGS_es_index));
  config->Set(string(kEsUsernameConfigKey),
              ::absl::GetFlag(FLAGS_es_username));
  config->Set(string(kEsPasswordConfigKey),
              ::absl::GetFlag(FLAGS_es_password));
  config->Set(string(kEsPasswordEnvConfigKey),
              ::absl::GetFlag(FLAGS_es_password_env));
  config->Set(string(kEsCaCertPathConfigKey),
              ::absl::GetFlag(FLAGS_es_ca_cert_path));
  config->Set(string(kEsVerifySslConfigKey),
              ::absl::GetFlag(FLAGS_es_verify_ssl) ? "true" : "false");
  config->Set(
      string(kEsRequestTimeoutMsConfigKey),
      ::std::to_string(::absl::GetFlag(FLAGS_es_request_timeout_ms)));
}

void ResolveLocalStoreConfigPaths(const string& executable_path,
                                  YamlConfigHelper* config) {
  if (config->GetString(kStoreTypeConfigKey) != string(kDefaultStoreType)) {
    return;
  }
  config->Set(
      string(kDataPathConfigKey),
      ::shooting_star::utilities::ResolveWorkspaceRelativePath(
          config->GetString(kDataPathConfigKey), executable_path));
}

bool IsSensitiveConfigKey(string_view key) {
  return key.find("password") != string_view::npos;
}

void LogResolvedConfig(const YamlConfigHelper& config) {
  vector<LogField> fields;
  for (const auto& [key, value] : config.values()) {
    if (IsSensitiveConfigKey(key)) {
      fields.push_back({key, kRedactedValue});
      continue;
    }
    fields.push_back({key, value});
  }
  const Logger& logger = LoggerRegistry::Get();
  logger.Info("resolved_config", fields);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    YamlConfigHelper config = LoadConfigFile(argc, argv);
    ApplyCommandLineOverridesToConfig(argc, argv, &config);
    ResolveLocalStoreConfigPaths(argv[0], &config);

    auto profile_logger = ::std::make_shared<Logger>(kServiceName);
    profile_logger->SetMinLogLevel(
        config.GetString(kServerLogLevelConfigKey, string(kDefaultLogLevel)));
    LoggerRegistry::Register(::std::move(profile_logger));
    LoggerRegistry::SetDefaultLoggerName(kServiceName);

    const Logger& logger = LoggerRegistry::Get();
    logger.Info("config_loaded",
                {
                    {"config_path", config.GetString(kConfigPathConfigKey)},
                });
    LogResolvedConfig(config);

    const string server_address =
        string(kListenAddressHost) + ":" +
        ::std::to_string(
            config.GetUInt16(kServerPortConfigKey, kDefaultServerPort));
    ProfileServiceImpl service(::std::move(config));

    ::grpc::EnableDefaultHealthCheckService(true);
    ::grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    ::grpc::ServerBuilder builder;
    builder.experimental().SetInterceptorCreators(
        ::shooting_star::utilities::CreateServerLoggingInterceptorCreators(
            logger));
    builder.AddListeningPort(server_address,
                             ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    ::std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    logger.Info(
      "server_started",
      {
        {"listen_address", server_address},
      }
    );
    server->Wait();
  } catch (const ::std::exception& ex) {
    const Logger& logger = LoggerRegistry::Get();
    logger.Error(
      "server_startup_failed",
      {
        {"error", ex.what()},
      }
    );
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
