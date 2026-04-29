#include "src/utilities/global_config/global_config.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "src/utilities/logger/logger.h"

namespace shooting_star {
namespace utilities {

class GlobalConfigTestAccess {
 public:
  static void Reset() { GlobalConfig::MutableGet().ResetToDefaults(); }
};

namespace {

using ::std::ofstream;
using ::std::string;
using ::std::filesystem::path;

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

TEST(GlobalConfigTest, InitializesDefaultsOnFirstGet) {
  GlobalConfigTestAccess::Reset();

  const GlobalConfig& config = GlobalConfig::Get();

  EXPECT_EQ(config.GetServerPort(), 50000);
  EXPECT_EQ(config.GetStoreType(), "local");
  EXPECT_EQ(config.GetLocalCacheCapacity(), 0);
  EXPECT_EQ(config.GetProfileServiceAddress(), "localhost:50100");
  EXPECT_EQ(config.GetListenAddress(), "0.0.0.0:50000");
}

TEST(GlobalConfigTest, InitializeSetsServiceName) {
  GlobalConfigTestAccess::Reset();

  const GlobalConfig& config = GlobalConfig::Initialize("profile");

  EXPECT_EQ(config.GetServiceName(), "profile");
}

TEST(GlobalConfigTest, InitializeRejectsDifferentServiceNameAfterFirstSet) {
  GlobalConfigTestAccess::Reset();

  (void)GlobalConfig::Initialize("profile");
  EXPECT_THROW((void)GlobalConfig::Initialize("gateway"),
               ::std::invalid_argument);
}

TEST(GlobalConfigTest, AppliesYamlOverDefaultsOnlyForConfiguredFields) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("profile_config.yaml");
  WriteFile(config_path,
            "server:\n"
            "  port: 51111\n"
            "elasticsearch:\n"
            "  http_client:\n"
            "    verify_ssl: false\n");

  ConfigYAML::ApplyFile(config_path.string());
  const GlobalConfig& config = GlobalConfig::Get();

  EXPECT_EQ(config.GetServerPort(), 51111);
  EXPECT_FALSE(config.GetElasticsearchHttpClientVerifySsl());
  EXPECT_EQ(config.GetStoreType(), "local");
  EXPECT_EQ(config.GetElasticsearchUsername(), "elastic");
}

TEST(GlobalConfigTest, AppliesOnlyExplicitCommandLineOverrides) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("argument_override_config.yaml");
  WriteFile(config_path,
            "server:\n"
            "  port: 50123\n"
            "  log_level: WARNING\n");
  ConfigYAML::ApplyFile(config_path.string());

  char arg0[] = "profile";
  char arg1[] = "--port=50124";
  char arg2[] = "--es_http_client_verify_ssl=false";
  char* argv[] = {arg0, arg1, arg2};
  ConfigArguments::Apply(3, argv);

  const GlobalConfig& config = GlobalConfig::Get();
  EXPECT_EQ(config.GetServerPort(), 50124);
  EXPECT_EQ(config.GetLogLevel(), "WARNING");
  EXPECT_FALSE(config.GetElasticsearchHttpClientVerifySsl());
}

TEST(GlobalConfigTest, SupportsGlobalLegacyArguments) {
  GlobalConfigTestAccess::Reset();

  char arg0[] = "gateway";
  char arg1[] = "--profile_service_host=profile.svc";
  char arg2[] = "--retrieval_service_port";
  char arg3[] = "60000";
  char* argv[] = {arg0, arg1, arg2, arg3};
  ConfigArguments::Apply(4, argv);

  const GlobalConfig& config = GlobalConfig::Get();
  EXPECT_EQ(config.GetProfileServiceAddress(), "profile.svc:50100");
  EXPECT_EQ(config.GetRetrievalServiceAddress(), "localhost:60000");
}

TEST(GlobalConfigTest, StartupFileUsesConfigPathArgument) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("startup_profile_config.yaml");
  WriteFile(config_path,
            "server:\n"
            "  port: 50155\n");

  char arg0[] = "profile";
  string config_arg = "--config_path=" + config_path.string();
  char* argv[] = {arg0, config_arg.data()};
  ConfigYAML::ApplyStartupFile(2, argv, arg0);

  const GlobalConfig& config = GlobalConfig::Get();
  EXPECT_EQ(config.GetConfigPath(), config_path.string());
  EXPECT_EQ(config.GetServerPort(), 50155);
}

TEST(GlobalConfigTest, RejectsUnknownYamlKeys) {
  GlobalConfigTestAccess::Reset();
  const path config_path = TestFilePath("unknown_key_config.yaml");
  WriteFile(config_path,
            "server:\n"
            "  unknown_port: 50100\n");

  EXPECT_THROW(ConfigYAML::ApplyFile(config_path.string()),
               ::std::invalid_argument);
}

TEST(GlobalConfigTest, RejectsInvalidCommandLineValues) {
  GlobalConfigTestAccess::Reset();

  char arg0[] = "gateway";
  char arg1[] = "--port=70000";
  char* argv[] = {arg0, arg1};

  EXPECT_THROW(ConfigArguments::Apply(2, argv), ::std::out_of_range);
}

TEST(GlobalConfigTest, LogsOnlyRequestedConfigSection) {
  GlobalConfigTestAccess::Reset();
  const GlobalConfig& config = GlobalConfig::Get();
  const Logger logger("global_config_test");

  ::testing::internal::CaptureStdout();
  config.LogResolvedConfigSection(logger, "local_cache");
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"resolved_config_section\""), string::npos);
  EXPECT_NE(logs.find("\"config_section\":\"local_cache\""), string::npos);
  EXPECT_NE(logs.find("\"local_cache.capacity\":\"0\""), string::npos);
  EXPECT_NE(logs.find("\"local_cache.ttl_seconds\":\"300\""), string::npos);
  EXPECT_EQ(logs.find("\"elasticsearch.base_url\":"), string::npos);
  EXPECT_EQ(logs.find("\"server.host\":"), string::npos);
}

TEST(GlobalConfigTest, LogsElasticsearchSectionAndRedactsPassword) {
  GlobalConfigTestAccess::Reset();

  char arg0[] = "profile";
  char arg1[] = "--es_password=super_secret";
  char* argv[] = {arg0, arg1};
  ConfigArguments::Apply(2, argv);

  const GlobalConfig& config = GlobalConfig::Get();
  const Logger logger("global_config_test");
  ::testing::internal::CaptureStdout();
  config.LogResolvedElasticsearchConfig(logger);
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"resolved_config_section\""), string::npos);
  EXPECT_NE(logs.find("\"config_section\":\"elasticsearch\""), string::npos);
  EXPECT_NE(logs.find("\"elasticsearch.base_url\":"), string::npos);
  EXPECT_NE(logs.find("\"elasticsearch.password\":\"<redacted>\""),
            string::npos);
  EXPECT_EQ(logs.find("super_secret"), string::npos);
  EXPECT_EQ(logs.find("\"local_cache.capacity\":"), string::npos);
  EXPECT_EQ(logs.find("\"server.host\":"), string::npos);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
