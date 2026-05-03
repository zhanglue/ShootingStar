#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shooting_star {
namespace utilities {

class ConfigArguments;
class ConfigYAML;
class GlobalConfigTestAccess;
class Logger;

class GlobalConfig final {
 public:
  static const GlobalConfig& Initialize(const ::std::string& service_name);
  static const GlobalConfig& Get();

  GlobalConfig(const GlobalConfig&) = delete;
  GlobalConfig& operator=(const GlobalConfig&) = delete;

  ::std::string GetConfigPath() const;
  ::std::string GetServerHost() const;
  uint16_t GetServerPort() const;
  ::std::string GetListenAddress() const;
  ::std::string GetLogLevel() const;

  // configs for the services as downstream dependencies
  ::std::string GetProfileServiceHost() const;
  uint16_t GetProfileServicePort() const;
  ::std::string GetProfileServiceAddress() const;
  ::std::string GetRetrievalServiceHost() const;
  uint16_t GetRetrievalServicePort() const;
  ::std::string GetRetrievalServiceAddress() const;
  double GetRetrievalRecallCandidateExpandRatio() const;
  ::std::string GetRetrieverItemCfHost() const;
  uint16_t GetRetrieverItemCfPort() const;
  ::std::string GetRetrieverItemCfAddress() const;
  ::std::string GetRetrieverUserCfHost() const;
  uint16_t GetRetrieverUserCfPort() const;
  ::std::string GetRetrieverUserCfAddress() const;
  int GetRetrieverMaxTriggerSeedCount() const;
  double GetRetrieverItemCfScoreMultiplier() const;
  double GetRetrieverUserCfScoreMultiplier() const;
  int GetUserCfTriggerSeedUserCount() const;

  ::std::string GetProfileStoreType() const;
  ::std::string GetProfileStoreDataPath() const;
  ::std::string GetSimilarityStoreType() const;
  ::std::string GetSimilarityDataPath() const;
  int GetLocalCacheCapacity() const;
  int GetLocalCacheTtlSeconds() const;
  ::std::string GetElasticsearchBaseUrl() const;
  ::std::string GetElasticsearchIndex() const;
  ::std::string GetElasticsearchUsername() const;
  ::std::string GetElasticsearchPassword() const;
  ::std::string GetElasticsearchPasswordEnv() const;
  int GetElasticsearchRequestTimeoutMs() const;
  int GetElasticsearchHttpClientPoolSize() const;
  int GetElasticsearchHttpClientAcquireTimeoutMs() const;
  int GetElasticsearchHttpClientRequestTimeoutMs() const;
  int GetElasticsearchHttpClientConnectTimeoutMs() const;
  int GetElasticsearchHttpClientAcquireRetryMaxAttempts() const;
  int GetElasticsearchHttpClientAcquireRetryDelayMs() const;
  int GetElasticsearchHttpClientConnectRetryMaxAttempts() const;
  int GetElasticsearchHttpClientConnectRetryDelayMs() const;
  int GetElasticsearchHttpClientRequestRetryMaxAttempts() const;
  int GetElasticsearchHttpClientRequestRetryDelayMs() const;
  bool GetElasticsearchHttpClientFollowRedirects() const;
  bool GetElasticsearchHttpClientVerifySsl() const;
  ::std::string GetElasticsearchHttpClientCaCertPath() const;

  ::std::string GetRedisHost() const;
  uint16_t GetRedisPort() const;
  int GetRedisDb() const;
  ::std::string GetRedisUsername() const;
  ::std::string GetRedisPassword() const;
  ::std::string GetRedisPasswordEnv() const;
  ::std::string GetRedisKeyPrefix() const;
  int GetRedisConnectTimeoutMs() const;
  int GetRedisSocketTimeoutMs() const;
  int GetRedisPoolSize() const;
  int GetRedisPoolWaitTimeoutMs() const;
  int GetRedisRetryMaxAttempts() const;
  int GetRedisRetryDelayMs() const;
  int GetRedisCommandBatchSize() const;

  ::std::string_view GetLocalCacheCapacityKey() const;
  ::std::string_view GetLocalCacheTtlSecondsKey() const;
  ::std::string_view GetElasticsearchRequestTimeoutMsKey() const;
  ::std::string_view GetElasticsearchHttpClientAcquireTimeoutMsKey() const;
  ::std::string_view GetElasticsearchHttpClientRequestTimeoutMsKey() const;
  ::std::string_view GetElasticsearchHttpClientConnectTimeoutMsKey() const;

  ::std::vector<::std::pair<::std::string, ::std::string>> GetResolvedValues()
      const;
  ::std::string_view GetServiceName() const;
  static bool IsSensitiveConfigKey(::std::string_view key);
  void LogResolvedConfig(const Logger& logger) const;
  void LogResolvedConfigSection(const Logger& logger,
                                ::std::string_view config_key_prefix) const;

 private:
  GlobalConfig();

  static GlobalConfig& MutableGet();
  static ::std::string_view Key(int field);

  void ResetToDefaults();
  void Set(int field, ::std::string value);

  ::std::string GetString(int field) const;
  int GetInt(int field) const;
  double GetDouble(int field) const;
  int GetNonNegativeInt(int field) const;
  int GetPositiveInt(int field) const;
  double GetPositiveDouble(int field) const;
  uint16_t GetUInt16(int field) const;
  bool GetBool(int field) const;
  ::std::string GetAddress(int host_field, int port_field) const;

  ::std::map<int, ::std::string> values_;
  ::std::string service_name_;

  friend class ConfigArguments;
  friend class ConfigYAML;
  friend class GlobalConfigTestAccess;
};

class ConfigYAML final {
 public:
  static void ApplyStartupFile(int argc, char** argv,
                               ::std::string_view executable_path);
  static void ApplyFile(const ::std::string& file_path);
  static void ApplyFileIfExists(const ::std::string& file_path);

  ConfigYAML() = delete;
};

class ConfigArguments final {
 public:
  static void Apply(int argc, char** argv);

  ConfigArguments() = delete;
};

}  // namespace utilities
}  // namespace shooting_star
