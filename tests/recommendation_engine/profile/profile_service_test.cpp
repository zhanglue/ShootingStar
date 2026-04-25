#include "src/recommendation_engine/profile/profile_service.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "src/utilities/config_helper/config_helper.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace recommendation_engine {
namespace {

using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::shooting_star::utilities::YamlConfigHelper;
using ::std::make_shared;
using ::std::string;

constexpr const char* kProfileDataRelativePath =
    "tests/testdata/recommendation_engine/profile/demo_profiles.json";

YamlConfigHelper CreateBaseConfig() {
  YamlConfigHelper config;
  config.Set("store_type", "local");
  config.Set("data_path",
             ResolveWorkspaceRelativePath(kProfileDataRelativePath));
  return config;
}

void InstallProfileTestLogger() {
  LoggerRegistry::ClearForTest();
  LoggerRegistry::Register(make_shared<Logger>("profile_test"));
  LoggerRegistry::SetDefaultLoggerName("profile_test");
}

TEST(ProfileServiceImplTest, LogsLocalCacheConfigWhenEnabled) {
  YamlConfigHelper config = CreateBaseConfig();
  config.Set("local_cache.capacity", "30");
  config.Set("local_cache.ttl_seconds", "300");
  InstallProfileTestLogger();

  ::testing::internal::CaptureStdout();
  ProfileServiceImpl service(::std::move(config));
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"profile_local_cache_initialized\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_local_cache_capacity\":\"30\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_local_cache_ttl_seconds\":\"300\""),
            string::npos);
}

TEST(ProfileServiceImplTest, LogsReasonWhenLocalCacheIsNotConfigured) {
  YamlConfigHelper config = CreateBaseConfig();
  InstallProfileTestLogger();

  ::testing::internal::CaptureStdout();
  ProfileServiceImpl service(::std::move(config));
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"profile_local_cache_disabled\""),
            string::npos);
  EXPECT_NE(logs.find("\"reason\":\"local_cache.capacity is not configured\""),
            string::npos);
}

TEST(ProfileServiceImplTest, LogsExplicitElasticsearchHttpClientConfigChain) {
  YamlConfigHelper config;
  config.Set("store_type", "elasticsearch");
  config.Set("elasticsearch.base_url", "http://localhost:9200");
  config.Set("elasticsearch.index", "profiles");
  config.Set("elasticsearch.request_timeout_ms", "7000");
  config.Set("elasticsearch.http_client.curl_handle_pool.pool_size", "2");
  config.Set(
      "elasticsearch.http_client.curl_handle_pool.acquire_timeout_ms",
      "500");
  config.Set("elasticsearch.http_client.request_timeout_ms", "6000");
  config.Set("elasticsearch.http_client.connect_timeout_ms", "2000");
  config.Set("elasticsearch.http_client.follow_redirects", "false");
  config.Set("elasticsearch.http_client.verify_ssl", "false");
  config.Set("elasticsearch.http_client.ca_cert_path", "/tmp/ca.crt");
  InstallProfileTestLogger();

  ::testing::internal::CaptureStdout();
  ProfileServiceImpl service(::std::move(config));
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"profile_store_initialized\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_store_type\":\"elasticsearch\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_request_timeout_ms\":\"7000\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_curl_handle_pool_size\":\"2\""),
            string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_curl_handle_pool_"
                "acquire_timeout_ms\":\"500\""),
      string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_request_timeout_ms\":\"6000\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_connect_timeout_ms\":\"2000\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_follow_redirects\":\"false\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_verify_ssl\":\"false\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_ca_cert_path\":\"/tmp/ca.crt\""),
            string::npos);
}

TEST(ProfileServiceImplTest, UsesDefaultElasticsearchTimeoutBudget) {
  YamlConfigHelper config;
  config.Set("store_type", "elasticsearch");
  config.Set("elasticsearch.base_url", "http://localhost:9200");
  config.Set("elasticsearch.index", "profiles");
  InstallProfileTestLogger();

  ::testing::internal::CaptureStdout();
  ProfileServiceImpl service(::std::move(config));
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"profile_es_request_timeout_ms\":\"100\""),
            string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_curl_handle_pool_"
                "acquire_timeout_ms\":\"30\""),
      string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_request_timeout_ms\":\"30\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_connect_timeout_ms\":\"20\""),
            string::npos);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchHttpClientAcquireTimeoutAboveRequestTimeout) {
  YamlConfigHelper config;
  config.Set("store_type", "elasticsearch");
  config.Set("elasticsearch.base_url", "http://localhost:9200");
  config.Set("elasticsearch.index", "profiles");
  config.Set("elasticsearch.request_timeout_ms", "7000");
  config.Set(
      "elasticsearch.http_client.curl_handle_pool.acquire_timeout_ms",
      "7001");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(::std::move(config)),
               ::std::invalid_argument);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchHttpClientRequestTimeoutAboveRequestTimeout) {
  YamlConfigHelper config;
  config.Set("store_type", "elasticsearch");
  config.Set("elasticsearch.base_url", "http://localhost:9200");
  config.Set("elasticsearch.index", "profiles");
  config.Set("elasticsearch.request_timeout_ms", "7000");
  config.Set("elasticsearch.http_client.request_timeout_ms", "7001");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(::std::move(config)),
               ::std::invalid_argument);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchHttpClientConnectTimeoutAboveHttpRequestTimeout) {
  YamlConfigHelper config;
  config.Set("store_type", "elasticsearch");
  config.Set("elasticsearch.base_url", "http://localhost:9200");
  config.Set("elasticsearch.index", "profiles");
  config.Set("elasticsearch.request_timeout_ms", "7000");
  config.Set("elasticsearch.http_client.request_timeout_ms", "6000");
  config.Set("elasticsearch.http_client.connect_timeout_ms", "6001");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(::std::move(config)),
               ::std::invalid_argument);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchAcquirePlusHttpRequestTimeoutAboveRequestTimeout) {
  YamlConfigHelper config;
  config.Set("store_type", "elasticsearch");
  config.Set("elasticsearch.base_url", "http://localhost:9200");
  config.Set("elasticsearch.index", "profiles");
  config.Set("elasticsearch.request_timeout_ms", "100");
  config.Set(
      "elasticsearch.http_client.curl_handle_pool.acquire_timeout_ms",
      "80");
  config.Set("elasticsearch.http_client.request_timeout_ms", "30");
  config.Set("elasticsearch.http_client.connect_timeout_ms", "20");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(::std::move(config)),
               ::std::invalid_argument);
}

}  // namespace
}  // namespace recommendation_engine
