#include "src/utilities/global_config/global_config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

#include "src/utilities/logger/logger.h"

namespace shooting_star {
namespace utilities {

using ::std::invalid_argument;
using ::std::nullopt;
using ::std::optional;
using ::std::out_of_range;
using ::std::string;
using ::std::string_view;
using ::std::vector;

namespace {

constexpr string_view kRedactedValue = "<redacted>";
constexpr string_view kElasticsearchConfigKeyPrefix = "elasticsearch";
constexpr string_view kRedisConfigKeyPrefix = "redis";

bool BelongsToConfigSection(string_view key, string_view config_key_prefix) {
  if (config_key_prefix.empty()) {
    return true;
  }
  if (!key.starts_with(config_key_prefix)) {
    return false;
  }
  return key.size() == config_key_prefix.size() ||
         key[config_key_prefix.size()] == '.';
}

enum class ValueType {
  kString,
  kInt,
  kDouble,
  kUInt16,
  kBool,
};

enum Field {
  kConfigPath,
  kServerHost,
  kServerPort,
  kLogLevel,
  kProfileServiceHost,
  kProfileServicePort,
  kRetrievalServiceHost,
  kRetrievalServicePort,
  kRetrievalRecallCandidateExpandRatio,
  kRetrieverItemCfHost,
  kRetrieverItemCfPort,
  kRetrieverUserCfHost,
  kRetrieverUserCfPort,
  kRetrieverMaxTriggerSeedCount,
  kUserCfTriggerSeedUserCount,
  kStoreType,
  kDataPath,
  kLocalCacheCapacity,
  kLocalCacheTtlSeconds,
  kElasticsearchBaseUrl,
  kElasticsearchIndex,
  kElasticsearchUsername,
  kElasticsearchPassword,
  kElasticsearchPasswordEnv,
  kElasticsearchRequestTimeoutMs,
  kElasticsearchHttpClientPoolSize,
  kElasticsearchHttpClientAcquireTimeoutMs,
  kElasticsearchHttpClientRequestTimeoutMs,
  kElasticsearchHttpClientConnectTimeoutMs,
  kElasticsearchHttpClientAcquireRetryMaxAttempts,
  kElasticsearchHttpClientAcquireRetryDelayMs,
  kElasticsearchHttpClientConnectRetryMaxAttempts,
  kElasticsearchHttpClientConnectRetryDelayMs,
  kElasticsearchHttpClientRequestRetryMaxAttempts,
  kElasticsearchHttpClientRequestRetryDelayMs,
  kElasticsearchHttpClientFollowRedirects,
  kElasticsearchHttpClientVerifySsl,
  kElasticsearchHttpClientCaCertPath,
  kRedisHost,
  kRedisPort,
  kRedisDb,
  kRedisUsername,
  kRedisPassword,
  kRedisPasswordEnv,
  kRedisKeyPrefix,
  kRedisConnectTimeoutMs,
  kRedisSocketTimeoutMs,
  kRedisPoolSize,
  kRedisPoolWaitTimeoutMs,
  kRedisRetryMaxAttempts,
  kRedisRetryDelayMs,
  kRedisCommandBatchSize,
};

struct ConfigEntry {
  int field;
  string_view key;
  string_view argument_name;
  ValueType value_type;
  string_view default_value;
};

constexpr ConfigEntry kConfigEntries[] = {
    {kConfigPath, "config.path", "config_path", ValueType::kString,
     "config.debug.yaml"},
    {kServerHost, "server.host", "host", ValueType::kString, "0.0.0.0"},
    {kServerPort, "server.port", "port", ValueType::kUInt16, "50000"},
    {kLogLevel, "server.log_level", "log_level", ValueType::kString, "INFO"},
    {kProfileServiceHost, "profile_service.host", "profile_service_host",
     ValueType::kString, "localhost"},
    {kProfileServicePort, "profile_service.port", "profile_service_port",
     ValueType::kUInt16, "50100"},
    {kRetrievalServiceHost, "retrieval_service.host", "retrieval_service_host",
     ValueType::kString, "localhost"},
    {kRetrievalServicePort, "retrieval_service.port", "retrieval_service_port",
     ValueType::kUInt16, "50200"},
    {kRetrievalRecallCandidateExpandRatio,
     "retrieval.recall_candidate_expand_ratio",
     "recall_candidate_expand_ratio", ValueType::kDouble, "1.0"},
    {kRetrieverItemCfHost, "retriever_item_cf.host", "retriever_item_cf_host",
     ValueType::kString, "localhost"},
    {kRetrieverItemCfPort, "retriever_item_cf.port", "retriever_item_cf_port",
     ValueType::kUInt16, "50210"},
    {kRetrieverUserCfHost, "retriever_user_cf.host", "retriever_user_cf_host",
     ValueType::kString, "localhost"},
    {kRetrieverUserCfPort, "retriever_user_cf.port", "retriever_user_cf_port",
     ValueType::kUInt16, "50220"},
    {kRetrieverMaxTriggerSeedCount, "retriever.max_trigger_seed_count",
     "retriever_max_trigger_seed_count", ValueType::kInt, "24"},
    {kUserCfTriggerSeedUserCount, "retriever_user_cf.trigger_seed_user_count",
     "user_cf_trigger_seed_user_count", ValueType::kInt, "10"},
    {kStoreType, "store_type", "store_type", ValueType::kString, "local"},
    {kDataPath, "data_path", "data_path", ValueType::kString,
     "tests/testdata/recommendation_engine/profile/demo_profiles.json"},
    {kLocalCacheCapacity, "local_cache.capacity", "cache_capacity",
     ValueType::kInt, "0"},
    {kLocalCacheTtlSeconds, "local_cache.ttl_seconds", "cache_ttl_seconds",
     ValueType::kInt, "300"},
    {kElasticsearchBaseUrl, "elasticsearch.base_url", "es_base_url",
     ValueType::kString, ""},
    {kElasticsearchIndex, "elasticsearch.index", "es_index", ValueType::kString,
     "movielens_32m_user_profile"},
    {kElasticsearchUsername, "elasticsearch.username", "es_username",
     ValueType::kString, "elastic"},
    {kElasticsearchPassword, "elasticsearch.password", "es_password",
     ValueType::kString, ""},
    {kElasticsearchPasswordEnv, "elasticsearch.password_env", "es_password_env",
     ValueType::kString, "ES_PASSWORD"},
    {kElasticsearchRequestTimeoutMs, "elasticsearch.request_timeout_ms",
     "es_request_timeout_ms", ValueType::kInt, "100"},
    {kElasticsearchHttpClientPoolSize,
     "elasticsearch.http_client.curl_handle_pool.pool_size",
     "es_http_client_curl_handle_pool_size", ValueType::kInt, "4"},
    {kElasticsearchHttpClientAcquireTimeoutMs,
     "elasticsearch.http_client.curl_handle_pool.acquire_timeout_ms",
     "es_http_client_curl_handle_pool_acquire_timeout_ms", ValueType::kInt,
     "30"},
    {kElasticsearchHttpClientRequestTimeoutMs,
     "elasticsearch.http_client.request_timeout_ms",
     "es_http_client_request_timeout_ms", ValueType::kInt, "30"},
    {kElasticsearchHttpClientConnectTimeoutMs,
     "elasticsearch.http_client.connect_timeout_ms",
     "es_http_client_connect_timeout_ms", ValueType::kInt, "20"},
    {kElasticsearchHttpClientAcquireRetryMaxAttempts,
     "elasticsearch.http_client.curl_handle_pool.retry.max_attempts",
     "es_http_client_curl_handle_pool_retry_max_attempts", ValueType::kInt,
     "3"},
    {kElasticsearchHttpClientAcquireRetryDelayMs,
     "elasticsearch.http_client.curl_handle_pool.retry.delay_ms",
     "es_http_client_curl_handle_pool_retry_delay_ms", ValueType::kInt, "0"},
    {kElasticsearchHttpClientConnectRetryMaxAttempts,
     "elasticsearch.http_client.connect_retry.max_attempts",
     "es_http_client_connect_retry_max_attempts", ValueType::kInt, "3"},
    {kElasticsearchHttpClientConnectRetryDelayMs,
     "elasticsearch.http_client.connect_retry.delay_ms",
     "es_http_client_connect_retry_delay_ms", ValueType::kInt, "0"},
    {kElasticsearchHttpClientRequestRetryMaxAttempts,
     "elasticsearch.http_client.request_retry.max_attempts",
     "es_http_client_request_retry_max_attempts", ValueType::kInt, "3"},
    {kElasticsearchHttpClientRequestRetryDelayMs,
     "elasticsearch.http_client.request_retry.delay_ms",
     "es_http_client_request_retry_delay_ms", ValueType::kInt, "0"},
    {kElasticsearchHttpClientFollowRedirects,
     "elasticsearch.http_client.follow_redirects",
     "es_http_client_follow_redirects", ValueType::kBool, "true"},
    {kElasticsearchHttpClientVerifySsl, "elasticsearch.http_client.verify_ssl",
     "es_http_client_verify_ssl", ValueType::kBool, "true"},
    {kElasticsearchHttpClientCaCertPath,
     "elasticsearch.http_client.ca_cert_path", "es_http_client_ca_cert_path",
     ValueType::kString, ""},
    {kRedisHost, "redis.host", "redis_host", ValueType::kString,
     "localhost"},
    {kRedisPort, "redis.port", "redis_port", ValueType::kUInt16, "6379"},
    {kRedisDb, "redis.db", "redis_db", ValueType::kInt, "0"},
    {kRedisUsername, "redis.username", "redis_username", ValueType::kString,
     ""},
    {kRedisPassword, "redis.password", "redis_password", ValueType::kString,
     ""},
    {kRedisPasswordEnv, "redis.password_env", "redis_password_env",
     ValueType::kString, "REDIS_PASSWORD"},
    {kRedisKeyPrefix, "redis.key_prefix", "redis_key_prefix",
     ValueType::kString, "rec:item_cf:v1:neighbors"},
    {kRedisConnectTimeoutMs, "redis.connect_timeout_ms",
     "redis_connect_timeout_ms", ValueType::kInt, "100"},
    {kRedisSocketTimeoutMs, "redis.socket_timeout_ms",
     "redis_socket_timeout_ms", ValueType::kInt, "100"},
    {kRedisPoolSize, "redis.pool_size", "redis_pool_size", ValueType::kInt,
     "4"},
    {kRedisPoolWaitTimeoutMs, "redis.pool_wait_timeout_ms",
     "redis_pool_wait_timeout_ms", ValueType::kInt, "50"},
    {kRedisRetryMaxAttempts, "redis.retry.max_attempts",
     "redis_retry_max_attempts", ValueType::kInt, "2"},
    {kRedisRetryDelayMs, "redis.retry.delay_ms", "redis_retry_delay_ms",
     ValueType::kInt, "10"},
    {kRedisCommandBatchSize, "redis.command_batch_size",
     "redis_command_batch_size", ValueType::kInt, "8"},
};

const ConfigEntry& EntryForField(int field) {
  for (const ConfigEntry& entry : kConfigEntries) {
    if (entry.field == field) {
      return entry;
    }
  }
  throw invalid_argument("Unknown global config field");
}

optional<ConfigEntry> FindEntryByYamlKey(string_view key) {
  for (const ConfigEntry& entry : kConfigEntries) {
    if (entry.key == key) {
      return entry;
    }
  }
  return nullopt;
}

optional<ConfigEntry> FindEntryByArgumentName(string_view argument_name) {
  for (const ConfigEntry& entry : kConfigEntries) {
    if (entry.argument_name == argument_name || entry.key == argument_name) {
      return entry;
    }
  }
  return nullopt;
}

string JoinKey(string_view prefix, string_view key) {
  if (prefix.empty()) {
    return string(key);
  }
  string joined(prefix);
  joined.push_back('.');
  joined.append(key);
  return joined;
}

void FlattenYamlNode(const YAML::Node& node, string_view prefix,
                     ::std::map<string, string>* values) {
  if (!node || node.IsNull()) {
    return;
  }

  if (node.IsScalar()) {
    if (prefix.empty()) {
      throw invalid_argument(
          "YAML root scalar cannot be converted to keyed config");
    }
    (*values)[string(prefix)] = node.Scalar();
    return;
  }

  if (node.IsMap()) {
    for (const auto& entry : node) {
      if (!entry.first.IsScalar()) {
        throw invalid_argument("YAML config keys must be scalars");
      }
      FlattenYamlNode(entry.second, JoinKey(prefix, entry.first.Scalar()),
                      values);
    }
    return;
  }

  if (node.IsSequence()) {
    if (prefix.empty()) {
      throw invalid_argument(
          "YAML root sequence cannot be converted to keyed config");
    }
    for (size_t index = 0; index < node.size(); ++index) {
      FlattenYamlNode(node[index], JoinKey(prefix, ::std::to_string(index)),
                      values);
    }
    return;
  }

  throw invalid_argument("Unsupported YAML node type");
}

string Lowercase(string_view value) {
  string normalized(value);
  ::std::transform(
      normalized.begin(), normalized.end(), normalized.begin(),
      [](unsigned char ch) { return static_cast<char>(::std::tolower(ch)); });
  return normalized;
}

int ParseIntValue(string_view key, string_view value) {
  int parsed = 0;
  const char* begin = value.data();
  const char* end = begin + value.size();
  const ::std::from_chars_result result = ::std::from_chars(begin, end, parsed);
  if (result.ec != ::std::errc() || result.ptr != end) {
    throw invalid_argument("Invalid integer config value for key: " +
                           string(key));
  }
  return parsed;
}

bool ParseBoolValue(string_view key, string_view value) {
  const string normalized = Lowercase(value);
  if (normalized == "true" || normalized == "1" || normalized == "yes" ||
      normalized == "on") {
    return true;
  }
  if (normalized == "false" || normalized == "0" || normalized == "no" ||
      normalized == "off") {
    return false;
  }
  throw invalid_argument("Invalid boolean config value for key: " +
                         string(key));
}

double ParseDoubleValue(string_view key, string_view value) {
  const string raw(value);
  size_t parsed_length = 0;
  double parsed = 0.0;
  try {
    parsed = ::std::stod(raw, &parsed_length);
  } catch (const ::std::invalid_argument&) {
    throw invalid_argument("Invalid double config value for key: " +
                           string(key));
  } catch (const ::std::out_of_range&) {
    throw out_of_range("Double config value is out of range for key: " +
                       string(key));
  }

  if (parsed_length != raw.size() || !::std::isfinite(parsed)) {
    throw invalid_argument("Invalid double config value for key: " +
                           string(key));
  }
  return parsed;
}

string CanonicalizeValue(const ConfigEntry& entry, string_view value) {
  switch (entry.value_type) {
    case ValueType::kString:
      return string(value);
    case ValueType::kInt:
      return ::std::to_string(ParseIntValue(entry.key, value));
    case ValueType::kDouble:
      return ::std::to_string(ParseDoubleValue(entry.key, value));
    case ValueType::kUInt16: {
      const int parsed = ParseIntValue(entry.key, value);
      if (parsed < 0 || parsed > ::std::numeric_limits<uint16_t>::max()) {
        throw out_of_range("Config value is outside uint16_t range for key: " +
                           string(entry.key));
      }
      return ::std::to_string(parsed);
    }
    case ValueType::kBool:
      return ParseBoolValue(entry.key, value) ? "true" : "false";
  }
  return string(value);
}

bool StartsWith(string_view value, string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

void ReadArgumentValue(int argc, char** argv, int* index, string_view name,
                       optional<string_view> inline_value,
                       const ConfigEntry& entry, string* value) {
  if (inline_value.has_value()) {
    *value = string(*inline_value);
    return;
  }
  if (entry.value_type == ValueType::kBool) {
    *value = "true";
    return;
  }
  if (*index + 1 >= argc) {
    throw invalid_argument("Missing value for command line argument: --" +
                           string(name));
  }
  ++(*index);
  *value = argv[*index];
}

struct StartupConfigPath {
  string path;
  bool from_command_line = false;
};

optional<StartupConfigPath> FindStartupConfigPath(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const string_view raw_arg(argv[i]);
    if (!StartsWith(raw_arg, "--")) {
      continue;
    }
    string_view arg = raw_arg.substr(2);
    const size_t equal_pos = arg.find('=');
    const string_view name =
        equal_pos == string_view::npos ? arg : arg.substr(0, equal_pos);
    const optional<string_view> inline_value =
        equal_pos == string_view::npos
            ? nullopt
            : optional<string_view>(arg.substr(equal_pos + 1));

    const optional<ConfigEntry> entry = FindEntryByArgumentName(name);
    if (!entry.has_value() || entry->field != kConfigPath) {
      continue;
    }

    if (inline_value.has_value()) {
      return StartupConfigPath{string(*inline_value), true};
    }
    if (i + 1 >= argc) {
      throw invalid_argument("Missing value for command line argument: --" +
                             string(name));
    }
    return StartupConfigPath{argv[i + 1], true};
  }

  return StartupConfigPath{GlobalConfig::Get().GetConfigPath(), false};
}

string ResolveStartupConfigPath(const StartupConfigPath& startup_config_path,
                                string_view executable_path) {
  if (startup_config_path.path.empty()) {
    return startup_config_path.path;
  }

  const ::std::filesystem::path config_file_path(startup_config_path.path);
  if (config_file_path.is_absolute()) {
    return startup_config_path.path;
  }

  const ::std::filesystem::path executable_relative_path =
      (::std::filesystem::absolute(::std::filesystem::path(executable_path))
           .parent_path() /
       config_file_path)
          .lexically_normal();
  if (startup_config_path.from_command_line ||
      ::std::filesystem::exists(executable_relative_path)) {
    return executable_relative_path.string();
  }

  const ::std::filesystem::path current_path_relative =
      (::std::filesystem::current_path() / config_file_path).lexically_normal();
  if (::std::filesystem::exists(current_path_relative)) {
    return current_path_relative.string();
  }

  return executable_relative_path.string();
}

}  // namespace

GlobalConfig::GlobalConfig() { ResetToDefaults(); }

const GlobalConfig& GlobalConfig::Initialize(const string& service_name) {
  if (service_name.empty()) {
    throw invalid_argument("service_name must not be empty");
  }

  GlobalConfig& config = MutableGet();
  if (config.service_name_.empty()) {
    config.service_name_ = service_name;
    return config;
  }
  if (config.service_name_ != service_name) {
    throw invalid_argument("GlobalConfig already initialized for service '" +
                           config.service_name_ +
                           "', cannot re-initialize with '" + service_name +
                           "'");
  }
  return config;
}

const GlobalConfig& GlobalConfig::Get() { return MutableGet(); }

GlobalConfig& GlobalConfig::MutableGet() {
  static GlobalConfig config;
  return config;
}

string_view GlobalConfig::Key(int field) { return EntryForField(field).key; }

void GlobalConfig::ResetToDefaults() {
  values_.clear();
  service_name_.clear();
  for (const ConfigEntry& entry : kConfigEntries) {
    values_[entry.field] = CanonicalizeValue(entry, entry.default_value);
  }
}

void GlobalConfig::Set(int field, string value) {
  const ConfigEntry& entry = EntryForField(field);
  values_[field] = CanonicalizeValue(entry, value);
}

string GlobalConfig::GetString(int field) const {
  const auto iter = values_.find(field);
  if (iter == values_.end()) {
    throw invalid_argument("Global config field has no value: " +
                           string(Key(field)));
  }
  return iter->second;
}

int GlobalConfig::GetInt(int field) const {
  return ParseIntValue(Key(field), GetString(field));
}

double GlobalConfig::GetDouble(int field) const {
  return ParseDoubleValue(Key(field), GetString(field));
}

int GlobalConfig::GetNonNegativeInt(int field) const {
  const int value = GetInt(field);
  if (value < 0) {
    throw invalid_argument(string(Key(field)) + " must not be negative");
  }
  return value;
}

int GlobalConfig::GetPositiveInt(int field) const {
  const int value = GetNonNegativeInt(field);
  if (value <= 0) {
    throw invalid_argument(string(Key(field)) + " must be greater than 0");
  }
  return value;
}

double GlobalConfig::GetPositiveDouble(int field) const {
  const double value = GetDouble(field);
  if (value <= 0.0) {
    throw invalid_argument(string(Key(field)) + " must be greater than 0");
  }
  return value;
}

uint16_t GlobalConfig::GetUInt16(int field) const {
  const int parsed = GetInt(field);
  if (parsed < 0 || parsed > ::std::numeric_limits<uint16_t>::max()) {
    throw out_of_range("Config value is outside uint16_t range for key: " +
                       string(Key(field)));
  }
  return static_cast<uint16_t>(parsed);
}

bool GlobalConfig::GetBool(int field) const {
  return ParseBoolValue(Key(field), GetString(field));
}

string GlobalConfig::GetAddress(int host_field, int port_field) const {
  return GetString(host_field) + ":" + ::std::to_string(GetUInt16(port_field));
}

string GlobalConfig::GetConfigPath() const { return GetString(kConfigPath); }

string GlobalConfig::GetServerHost() const { return GetString(kServerHost); }

uint16_t GlobalConfig::GetServerPort() const { return GetUInt16(kServerPort); }

string GlobalConfig::GetListenAddress() const {
  return GetAddress(kServerHost, kServerPort);
}

string GlobalConfig::GetLogLevel() const { return GetString(kLogLevel); }

string GlobalConfig::GetProfileServiceHost() const {
  return GetString(kProfileServiceHost);
}

uint16_t GlobalConfig::GetProfileServicePort() const {
  return GetUInt16(kProfileServicePort);
}

string GlobalConfig::GetProfileServiceAddress() const {
  return GetAddress(kProfileServiceHost, kProfileServicePort);
}

string GlobalConfig::GetRetrievalServiceHost() const {
  return GetString(kRetrievalServiceHost);
}

uint16_t GlobalConfig::GetRetrievalServicePort() const {
  return GetUInt16(kRetrievalServicePort);
}

string GlobalConfig::GetRetrievalServiceAddress() const {
  return GetAddress(kRetrievalServiceHost, kRetrievalServicePort);
}

double GlobalConfig::GetRetrievalRecallCandidateExpandRatio() const {
  return GetPositiveDouble(kRetrievalRecallCandidateExpandRatio);
}

string GlobalConfig::GetRetrieverItemCfHost() const {
  return GetString(kRetrieverItemCfHost);
}

uint16_t GlobalConfig::GetRetrieverItemCfPort() const {
  return GetUInt16(kRetrieverItemCfPort);
}

string GlobalConfig::GetRetrieverItemCfAddress() const {
  return GetAddress(kRetrieverItemCfHost, kRetrieverItemCfPort);
}

string GlobalConfig::GetRetrieverUserCfHost() const {
  return GetString(kRetrieverUserCfHost);
}

uint16_t GlobalConfig::GetRetrieverUserCfPort() const {
  return GetUInt16(kRetrieverUserCfPort);
}

string GlobalConfig::GetRetrieverUserCfAddress() const {
  return GetAddress(kRetrieverUserCfHost, kRetrieverUserCfPort);
}

int GlobalConfig::GetRetrieverMaxTriggerSeedCount() const {
  return GetPositiveInt(kRetrieverMaxTriggerSeedCount);
}

int GlobalConfig::GetUserCfTriggerSeedUserCount() const {
  return GetPositiveInt(kUserCfTriggerSeedUserCount);
}

string GlobalConfig::GetStoreType() const { return GetString(kStoreType); }

string GlobalConfig::GetDataPath() const { return GetString(kDataPath); }

int GlobalConfig::GetLocalCacheCapacity() const {
  return GetInt(kLocalCacheCapacity);
}

int GlobalConfig::GetLocalCacheTtlSeconds() const {
  return GetInt(kLocalCacheTtlSeconds);
}

string GlobalConfig::GetElasticsearchBaseUrl() const {
  return GetString(kElasticsearchBaseUrl);
}

string GlobalConfig::GetElasticsearchIndex() const {
  return GetString(kElasticsearchIndex);
}

string GlobalConfig::GetElasticsearchUsername() const {
  return GetString(kElasticsearchUsername);
}

string GlobalConfig::GetElasticsearchPassword() const {
  return GetString(kElasticsearchPassword);
}

string GlobalConfig::GetElasticsearchPasswordEnv() const {
  return GetString(kElasticsearchPasswordEnv);
}

int GlobalConfig::GetElasticsearchRequestTimeoutMs() const {
  return GetPositiveInt(kElasticsearchRequestTimeoutMs);
}

int GlobalConfig::GetElasticsearchHttpClientPoolSize() const {
  return GetPositiveInt(kElasticsearchHttpClientPoolSize);
}

int GlobalConfig::GetElasticsearchHttpClientAcquireTimeoutMs() const {
  return GetPositiveInt(kElasticsearchHttpClientAcquireTimeoutMs);
}

int GlobalConfig::GetElasticsearchHttpClientRequestTimeoutMs() const {
  return GetPositiveInt(kElasticsearchHttpClientRequestTimeoutMs);
}

int GlobalConfig::GetElasticsearchHttpClientConnectTimeoutMs() const {
  return GetPositiveInt(kElasticsearchHttpClientConnectTimeoutMs);
}

int GlobalConfig::GetElasticsearchHttpClientAcquireRetryMaxAttempts() const {
  return GetPositiveInt(kElasticsearchHttpClientAcquireRetryMaxAttempts);
}

int GlobalConfig::GetElasticsearchHttpClientAcquireRetryDelayMs() const {
  return GetNonNegativeInt(kElasticsearchHttpClientAcquireRetryDelayMs);
}

int GlobalConfig::GetElasticsearchHttpClientConnectRetryMaxAttempts() const {
  return GetPositiveInt(kElasticsearchHttpClientConnectRetryMaxAttempts);
}

int GlobalConfig::GetElasticsearchHttpClientConnectRetryDelayMs() const {
  return GetNonNegativeInt(kElasticsearchHttpClientConnectRetryDelayMs);
}

int GlobalConfig::GetElasticsearchHttpClientRequestRetryMaxAttempts() const {
  return GetPositiveInt(kElasticsearchHttpClientRequestRetryMaxAttempts);
}

int GlobalConfig::GetElasticsearchHttpClientRequestRetryDelayMs() const {
  return GetNonNegativeInt(kElasticsearchHttpClientRequestRetryDelayMs);
}

bool GlobalConfig::GetElasticsearchHttpClientFollowRedirects() const {
  return GetBool(kElasticsearchHttpClientFollowRedirects);
}

bool GlobalConfig::GetElasticsearchHttpClientVerifySsl() const {
  return GetBool(kElasticsearchHttpClientVerifySsl);
}

string GlobalConfig::GetElasticsearchHttpClientCaCertPath() const {
  return GetString(kElasticsearchHttpClientCaCertPath);
}

string GlobalConfig::GetRedisHost() const { return GetString(kRedisHost); }

uint16_t GlobalConfig::GetRedisPort() const { return GetUInt16(kRedisPort); }

int GlobalConfig::GetRedisDb() const { return GetNonNegativeInt(kRedisDb); }

string GlobalConfig::GetRedisUsername() const {
  return GetString(kRedisUsername);
}

string GlobalConfig::GetRedisPassword() const {
  return GetString(kRedisPassword);
}

string GlobalConfig::GetRedisPasswordEnv() const {
  return GetString(kRedisPasswordEnv);
}

string GlobalConfig::GetRedisKeyPrefix() const {
  return GetString(kRedisKeyPrefix);
}

int GlobalConfig::GetRedisConnectTimeoutMs() const {
  return GetNonNegativeInt(kRedisConnectTimeoutMs);
}

int GlobalConfig::GetRedisSocketTimeoutMs() const {
  return GetNonNegativeInt(kRedisSocketTimeoutMs);
}

int GlobalConfig::GetRedisPoolSize() const {
  return GetPositiveInt(kRedisPoolSize);
}

int GlobalConfig::GetRedisPoolWaitTimeoutMs() const {
  return GetNonNegativeInt(kRedisPoolWaitTimeoutMs);
}

int GlobalConfig::GetRedisRetryMaxAttempts() const {
  return GetPositiveInt(kRedisRetryMaxAttempts);
}

int GlobalConfig::GetRedisRetryDelayMs() const {
  return GetNonNegativeInt(kRedisRetryDelayMs);
}

int GlobalConfig::GetRedisCommandBatchSize() const {
  return GetPositiveInt(kRedisCommandBatchSize);
}

string_view GlobalConfig::GetLocalCacheCapacityKey() const {
  return Key(kLocalCacheCapacity);
}

string_view GlobalConfig::GetLocalCacheTtlSecondsKey() const {
  return Key(kLocalCacheTtlSeconds);
}

string_view GlobalConfig::GetElasticsearchRequestTimeoutMsKey() const {
  return Key(kElasticsearchRequestTimeoutMs);
}

string_view GlobalConfig::GetElasticsearchHttpClientAcquireTimeoutMsKey()
    const {
  return Key(kElasticsearchHttpClientAcquireTimeoutMs);
}

string_view GlobalConfig::GetElasticsearchHttpClientRequestTimeoutMsKey()
    const {
  return Key(kElasticsearchHttpClientRequestTimeoutMs);
}

string_view GlobalConfig::GetElasticsearchHttpClientConnectTimeoutMsKey()
    const {
  return Key(kElasticsearchHttpClientConnectTimeoutMs);
}

::std::vector<::std::pair<string, string>> GlobalConfig::GetResolvedValues()
    const {
  ::std::vector<::std::pair<string, string>> values;
  for (const ConfigEntry& entry : kConfigEntries) {
    values.emplace_back(entry.key, GetString(entry.field));
  }
  return values;
}

string_view GlobalConfig::GetServiceName() const {
  if (service_name_.empty()) {
    throw invalid_argument("GlobalConfig service_name is not initialized");
  }
  return service_name_;
}

bool GlobalConfig::IsSensitiveConfigKey(string_view key) {
  return key.find("password") != string_view::npos;
}

void GlobalConfig::LogResolvedConfig(const Logger& logger) const {
  const vector<::std::pair<string, string>> values = GetResolvedValues();
  vector<LogField> fields;
  fields.reserve(values.size());
  for (const auto& [key, value] : values) {
    fields.push_back(
        {key, IsSensitiveConfigKey(key) ? kRedactedValue : string_view(value)});
  }
  logger.Info(
    "resolved_config",
    fields);
}

void GlobalConfig::LogResolvedConfigSection(
    const Logger& logger, string_view config_key_prefix) const {
  const vector<::std::pair<string, string>> values = GetResolvedValues();
  vector<LogField> fields;
  fields.reserve(values.size() + 1);
  fields.push_back({"config_section", config_key_prefix});
  for (const auto& [key, value] : values) {
    if (!BelongsToConfigSection(key, config_key_prefix)) {
      continue;
    }
    fields.push_back(
        {key, IsSensitiveConfigKey(key) ? kRedactedValue : string_view(value)});
  }
  logger.Info(
    "resolved_config_section",
    fields);
}

void GlobalConfig::LogResolvedElasticsearchConfig(const Logger& logger) const {
  LogResolvedConfigSection(logger, kElasticsearchConfigKeyPrefix);
}

void GlobalConfig::LogResolvedRedisConfig(const Logger& logger) const {
  LogResolvedConfigSection(logger, kRedisConfigKeyPrefix);
}

void ConfigYAML::ApplyStartupFile(int argc, char** argv,
                                  string_view executable_path) {
  const optional<StartupConfigPath> startup_config_path =
      FindStartupConfigPath(argc, argv);
  if (!startup_config_path.has_value()) {
    return;
  }
  const string resolved_config_path =
      ResolveStartupConfigPath(*startup_config_path, executable_path);
  if (!startup_config_path->from_command_line &&
      !::std::filesystem::exists(resolved_config_path)) {
    return;
  }
  ApplyFile(resolved_config_path);
  GlobalConfig::MutableGet().Set(kConfigPath, resolved_config_path);
}

void ConfigYAML::ApplyFile(const string& file_path) {
  ::std::map<string, string> loaded_values;
  try {
    FlattenYamlNode(YAML::LoadFile(file_path), "", &loaded_values);
  } catch (const YAML::Exception& ex) {
    throw invalid_argument("Failed to load YAML config file '" + file_path +
                           "': " + ex.what());
  }

  for (const auto& [key, value] : loaded_values) {
    const optional<ConfigEntry> entry = FindEntryByYamlKey(key);
    if (!entry.has_value()) {
      throw invalid_argument("Unknown YAML config key: " + key);
    }
    GlobalConfig::MutableGet().Set(entry->field, value);
  }
}

void ConfigYAML::ApplyFileIfExists(const string& file_path) {
  ::std::error_code error;
  const bool exists = ::std::filesystem::exists(file_path, error);
  if (error) {
    throw invalid_argument("Failed to inspect YAML config file '" + file_path +
                           "': " + error.message());
  }
  if (!exists) {
    return;
  }
  ApplyFile(file_path);
}

void ConfigArguments::Apply(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const string_view raw_arg(argv[i]);
    if (raw_arg == "--") {
      return;
    }
    if (!StartsWith(raw_arg, "--")) {
      throw invalid_argument("Unexpected positional command line argument: " +
                             string(raw_arg));
    }

    string_view arg = raw_arg.substr(2);
    bool negated_bool = false;
    if (StartsWith(arg, "no")) {
      const string_view maybe_bool_name = arg.substr(2);
      const optional<ConfigEntry> maybe_bool_entry =
          FindEntryByArgumentName(maybe_bool_name);
      if (maybe_bool_entry.has_value() &&
          maybe_bool_entry->value_type == ValueType::kBool) {
        arg = maybe_bool_name;
        negated_bool = true;
      }
    }

    const size_t equal_pos = arg.find('=');
    const string_view name =
        equal_pos == string_view::npos ? arg : arg.substr(0, equal_pos);
    const optional<string_view> inline_value =
        equal_pos == string_view::npos
            ? nullopt
            : optional<string_view>(arg.substr(equal_pos + 1));

    const optional<ConfigEntry> entry = FindEntryByArgumentName(name);
    if (!entry.has_value()) {
      throw invalid_argument("Unknown command line argument: --" +
                             string(name));
    }

    string value;
    if (negated_bool) {
      if (inline_value.has_value()) {
        throw invalid_argument(
            "Negated boolean argument does not accept a "
            "value: --no" +
            string(name));
      }
      value = "false";
    } else {
      ReadArgumentValue(argc, argv, &i, name, inline_value, *entry, &value);
    }

    if (entry->field == kConfigPath) {
      continue;
    }
    GlobalConfig::MutableGet().Set(entry->field, value);
  }
}

}  // namespace utilities
}  // namespace shooting_star
