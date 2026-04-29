#include "src/recommendation_engine/profile/profile_service.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include "src/utilities/global_config/global_config.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/logger/logger_registry.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {

class GlobalConfigTestAccess {
 public:
  static void Reset() { GlobalConfig::MutableGet().ResetToDefaults(); }
};

}  // namespace utilities
}  // namespace shooting_star

namespace recommendation_engine {
namespace {

using ::shooting_star::utilities::ConfigYAML;
using ::shooting_star::utilities::GlobalConfig;
using ::shooting_star::utilities::GlobalConfigTestAccess;
using ::shooting_star::utilities::Logger;
using ::shooting_star::utilities::LoggerRegistry;
using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::make_shared;
using ::std::ofstream;
using ::std::string;
using ::std::filesystem::path;

constexpr const char* kProfileDataRelativePath =
    "tests/testdata/recommendation_engine/profile/demo_profiles.json";

path TestFilePath(const string& name) {
  const char* test_tmpdir = ::std::getenv("TEST_TMPDIR");
  if (test_tmpdir == nullptr) {
    return ::std::filesystem::current_path() / name;
  }
  return path(test_tmpdir) / name;
}

void WriteFile(const path& file_path, const string& content) {
  ::std::filesystem::create_directories(file_path.parent_path());
  ofstream fout(file_path);
  ASSERT_TRUE(fout.is_open());
  fout << content;
}

string QuoteYamlString(const string& value) {
  string quoted = "\"";
  for (const char ch : value) {
    if (ch == '\\' || ch == '"') {
      quoted.push_back('\\');
    }
    quoted.push_back(ch);
  }
  quoted.push_back('"');
  return quoted;
}

const GlobalConfig& ApplyProfileConfig(const string& yaml) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("profile_service_config.yaml");
  WriteFile(config_path, yaml);
  ConfigYAML::ApplyFile(config_path.string());
  return GlobalConfig::Get();
}

const GlobalConfig& CreateBaseConfig() {
  return ApplyProfileConfig(
      "store_type: local\n"
      "data_path: " +
      QuoteYamlString(ResolveWorkspaceRelativePath(kProfileDataRelativePath)) +
      "\n");
}

void InstallProfileTestLogger() {
  LoggerRegistry::ClearForTest();
  LoggerRegistry::Register(make_shared<Logger>("profile_test"));
  LoggerRegistry::SetDefaultLoggerName("profile_test");
}

TEST(ProfileServiceImplTest, LogsLocalCacheConfigWhenEnabled) {
  const GlobalConfig& config = ApplyProfileConfig(
      "store_type: local\n"
      "data_path: " +
      QuoteYamlString(ResolveWorkspaceRelativePath(kProfileDataRelativePath)) +
      "\n"
      "local_cache:\n"
      "  capacity: 30\n"
      "  ttl_seconds: 300\n");
  InstallProfileTestLogger();

  ::testing::internal::CaptureStdout();
  ProfileServiceImpl service(config);
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"profile_local_cache_initialized\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_local_cache_capacity\":\"30\""), string::npos);
  EXPECT_NE(logs.find("\"profile_local_cache_ttl_seconds\":\"300\""),
            string::npos);
}

TEST(ProfileServiceImplTest, LogsReasonWhenLocalCacheIsNotConfigured) {
  const GlobalConfig& config = CreateBaseConfig();
  InstallProfileTestLogger();

  ::testing::internal::CaptureStdout();
  ProfileServiceImpl service(config);
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"profile_local_cache_disabled\""),
            string::npos);
  EXPECT_NE(logs.find("\"reason\":\"local_cache.capacity must be greater "
                      "than 0\""),
            string::npos);
}

TEST(ProfileServiceImplTest, LogsExplicitElasticsearchHttpClientConfigChain) {
  const GlobalConfig& config = ApplyProfileConfig(
      "store_type: elasticsearch\n"
      "server:\n"
      "  get_profile_timeout_ms: 8000\n"
      "elasticsearch:\n"
      "  base_url: http://localhost:9200\n"
      "  index: profiles\n"
      "  request_timeout_ms: 7000\n"
      "  http_client:\n"
      "    curl_handle_pool:\n"
      "      pool_size: 2\n"
      "      acquire_timeout_ms: 500\n"
      "      retry:\n"
      "        max_attempts: 4\n"
      "        delay_ms: 1\n"
      "    request_timeout_ms: 6000\n"
      "    connect_timeout_ms: 2000\n"
      "    connect_retry:\n"
      "      max_attempts: 5\n"
      "      delay_ms: 2\n"
      "    request_retry:\n"
      "      max_attempts: 6\n"
      "      delay_ms: 3\n"
      "    follow_redirects: false\n"
      "    verify_ssl: false\n"
      "    ca_cert_path: /tmp/ca.crt\n");
  InstallProfileTestLogger();

  ::testing::internal::CaptureStdout();
  ProfileServiceImpl service(config);
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"profile_store_initialized\""), string::npos);
  EXPECT_NE(logs.find("\"profile_store_type\":\"elasticsearch\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_request_timeout_ms\":\"7000\""),
            string::npos);
  EXPECT_NE(logs.find("\"get_profile_timeout_ms\":\"8000\""), string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_curl_handle_pool_size\":\"2\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_curl_handle_pool_"
                      "acquire_timeout_ms\":\"500\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_request_timeout_ms\":\"6000\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_connect_timeout_ms\":\"2000\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_curl_handle_pool_"
                      "retry_max_attempts\":\"4\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_curl_handle_pool_"
                      "retry_delay_ms\":\"1\""),
            string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_connect_retry_max_attempts\":\"5\""),
      string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_connect_retry_delay_ms\":\"2\""),
      string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_request_retry_max_attempts\":\"6\""),
      string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_request_retry_delay_ms\":\"3\""),
      string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_follow_redirects\":\"false\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_verify_ssl\":\"false\""),
            string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_ca_cert_path\":\"/tmp/ca.crt\""),
      string::npos);
}

TEST(ProfileServiceImplTest, UsesDefaultElasticsearchTimeoutBudget) {
  const GlobalConfig& config = ApplyProfileConfig(
      "store_type: elasticsearch\n"
      "elasticsearch:\n"
      "  base_url: http://localhost:9200\n"
      "  index: profiles\n");
  InstallProfileTestLogger();

  ::testing::internal::CaptureStdout();
  ProfileServiceImpl service(config);
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"profile_es_request_timeout_ms\":\"100\""),
            string::npos);
  EXPECT_NE(logs.find("\"get_profile_timeout_ms\":\"120\""), string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_curl_handle_pool_"
                      "acquire_timeout_ms\":\"30\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_request_timeout_ms\":\"30\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_connect_timeout_ms\":\"20\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_curl_handle_pool_"
                      "retry_max_attempts\":\"3\""),
            string::npos);
  EXPECT_NE(logs.find("\"profile_es_http_client_curl_handle_pool_"
                      "retry_delay_ms\":\"0\""),
            string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_connect_retry_max_attempts\":\"3\""),
      string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_connect_retry_delay_ms\":\"0\""),
      string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_request_retry_max_attempts\":\"3\""),
      string::npos);
  EXPECT_NE(
      logs.find("\"profile_es_http_client_request_retry_delay_ms\":\"0\""),
      string::npos);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchRequestTimeoutAboveGetProfileTimeout) {
  const GlobalConfig& config = ApplyProfileConfig(
      "store_type: elasticsearch\n"
      "server:\n"
      "  get_profile_timeout_ms: 99\n"
      "elasticsearch:\n"
      "  base_url: http://localhost:9200\n"
      "  index: profiles\n"
      "  request_timeout_ms: 100\n");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(config), ::std::invalid_argument);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchHttpClientAcquireTimeoutAboveRequestTimeout) {
  const GlobalConfig& config = ApplyProfileConfig(
      "store_type: elasticsearch\n"
      "server:\n"
      "  get_profile_timeout_ms: 8000\n"
      "elasticsearch:\n"
      "  base_url: http://localhost:9200\n"
      "  index: profiles\n"
      "  request_timeout_ms: 7000\n"
      "  http_client:\n"
      "    curl_handle_pool:\n"
      "      acquire_timeout_ms: 7001\n");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(config), ::std::invalid_argument);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchHttpClientRequestTimeoutAboveRequestTimeout) {
  const GlobalConfig& config = ApplyProfileConfig(
      "store_type: elasticsearch\n"
      "server:\n"
      "  get_profile_timeout_ms: 8000\n"
      "elasticsearch:\n"
      "  base_url: http://localhost:9200\n"
      "  index: profiles\n"
      "  request_timeout_ms: 7000\n"
      "  http_client:\n"
      "    request_timeout_ms: 7001\n");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(config), ::std::invalid_argument);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchHttpClientConnectTimeoutAboveHttpRequestTimeout) {
  const GlobalConfig& config = ApplyProfileConfig(
      "store_type: elasticsearch\n"
      "server:\n"
      "  get_profile_timeout_ms: 8000\n"
      "elasticsearch:\n"
      "  base_url: http://localhost:9200\n"
      "  index: profiles\n"
      "  request_timeout_ms: 7000\n"
      "  http_client:\n"
      "    request_timeout_ms: 6000\n"
      "    connect_timeout_ms: 6001\n");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(config), ::std::invalid_argument);
}

TEST(ProfileServiceImplTest,
     RejectsElasticsearchAcquirePlusHttpRequestTimeoutAboveRequestTimeout) {
  const GlobalConfig& config = ApplyProfileConfig(
      "store_type: elasticsearch\n"
      "elasticsearch:\n"
      "  base_url: http://localhost:9200\n"
      "  index: profiles\n"
      "  request_timeout_ms: 100\n"
      "  http_client:\n"
      "    curl_handle_pool:\n"
      "      acquire_timeout_ms: 80\n"
      "    request_timeout_ms: 30\n"
      "    connect_timeout_ms: 20\n");
  InstallProfileTestLogger();

  EXPECT_THROW(ProfileServiceImpl service(config), ::std::invalid_argument);
}

}  // namespace
}  // namespace recommendation_engine
