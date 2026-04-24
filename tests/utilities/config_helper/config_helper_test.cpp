#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "src/utilities/config_helper/config_helper.h"

namespace shooting_star {
namespace utilities {
namespace {

using ::std::ofstream;
using ::std::filesystem::path;
using ::std::string;

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

TEST(YamlConfigHelperTest, LoadsNestedYamlAsDotSeparatedKeys) {
  const path config_path = TestFilePath("nested_config.yaml");
  WriteFile(config_path, "server:\n"
                         "  port: 50101\n"
                         "profile:\n"
                         "  store_type: local\n"
                         "  data_path: tests/testdata/profiles.json\n"
                         "  enabled: true\n");

  YamlConfigHelper config;
  config.LoadFromYamlFile(config_path.string());

  EXPECT_TRUE(config.Has("server.port"));
  EXPECT_EQ(config.GetUInt16("server.port", 0), 50101);
  EXPECT_EQ(config.GetString("profile.store_type"), "local");
  EXPECT_EQ(config.GetString("profile.data_path"),
            "tests/testdata/profiles.json");
  EXPECT_TRUE(config.GetBool("profile.enabled", false));
}

TEST(YamlConfigHelperTest, LoadsSequenceIndexesAsKeys) {
  const path config_path = TestFilePath("sequence_config.yaml");
  WriteFile(config_path, "retrievers:\n"
                         "  - item_cf\n"
                         "  - user_cf\n");

  YamlConfigHelper config;
  config.LoadFromYamlFile(config_path.string());

  EXPECT_EQ(config.GetString("retrievers.0"), "item_cf");
  EXPECT_EQ(config.GetString("retrievers.1"), "user_cf");
}

TEST(YamlConfigHelperTest, KeepsDefaultValueWhenKeyIsMissing) {
  YamlConfigHelper config;

  EXPECT_EQ(config.GetString("missing", "fallback"), "fallback");
  EXPECT_EQ(config.GetInt("missing", 42), 42);
  EXPECT_EQ(config.GetUInt16("missing", 123), 123);
  EXPECT_TRUE(config.GetBool("missing", true));
}

TEST(YamlConfigHelperTest, LoadIfExistsSkipsMissingFile) {
  YamlConfigHelper config;

  EXPECT_NO_THROW(config.LoadFromYamlFileIfExists(
      TestFilePath("missing_config.yaml").string()));
  EXPECT_FALSE(config.Has("server.port"));
}

TEST(YamlConfigHelperTest, ThrowsForInvalidTypedValues) {
  YamlConfigHelper config;
  config.Set("port", "70000");
  config.Set("enabled", "maybe");

  EXPECT_THROW(config.GetUInt16("port", 0), ::std::out_of_range);
  EXPECT_THROW(config.GetBool("enabled", false), ::std::invalid_argument);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
