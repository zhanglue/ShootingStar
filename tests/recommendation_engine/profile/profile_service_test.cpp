#include "src/recommendation_engine/profile/profile_service.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "src/utilities/config_helper/config_helper.h"
#include "src/utilities/logger/logger.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace recommendation_engine {
namespace {

using ::shooting_star::utilities::Logger;
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

TEST(ProfileServiceImplTest, LogsLocalCacheConfigWhenEnabled) {
  YamlConfigHelper config = CreateBaseConfig();
  config.Set("local_cache.capacity", "30");
  config.Set("local_cache.ttl_seconds", "300");

  ::testing::internal::CaptureStdout();
  const auto logger = make_shared<Logger>("profile_test");
  ProfileServiceImpl service(::std::move(config), logger);
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

  ::testing::internal::CaptureStdout();
  const auto logger = make_shared<Logger>("profile_test");
  ProfileServiceImpl service(::std::move(config), logger);
  (void)service;
  const string logs = ::testing::internal::GetCapturedStdout();

  EXPECT_NE(logs.find("\"event\":\"profile_local_cache_disabled\""),
            string::npos);
  EXPECT_NE(logs.find("\"reason\":\"local_cache.capacity is not configured\""),
            string::npos);
}

}  // namespace
}  // namespace recommendation_engine
