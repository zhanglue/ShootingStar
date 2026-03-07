#include <cstdlib>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {
namespace {

class RuntimeUtilitiesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* workspace_dir = ::std::getenv("BUILD_WORKSPACE_DIRECTORY");
    if (workspace_dir != nullptr) {
      original_workspace_dir_ = workspace_dir;
    }
  }

  void TearDown() override {
    if (original_workspace_dir_.has_value()) {
      ASSERT_EQ(setenv("BUILD_WORKSPACE_DIRECTORY", original_workspace_dir_->c_str(), 1), 0);
    } else {
      ASSERT_EQ(unsetenv("BUILD_WORKSPACE_DIRECTORY"), 0);
    }
  }

  ::std::optional<::std::string> original_workspace_dir_;
};

TEST_F(RuntimeUtilitiesTest, ReturnsConfiguredPathWhenProvided) {
  EXPECT_EQ(
      ResolveWorkspaceRelativePath("/tmp/profiles.json"),
      "/tmp/profiles.json");
}

TEST_F(RuntimeUtilitiesTest, UsesWorkspaceDirectoryWhenEnvironmentVariableExists) {
  ASSERT_EQ(setenv("BUILD_WORKSPACE_DIRECTORY", "/workspace/shooting_star", 1), 0);

  EXPECT_EQ(
      ResolveWorkspaceRelativePath("tests/testdata/recommender_engine/profile/demo_profiles.json"),
      "/workspace/shooting_star/tests/testdata/recommender_engine/profile/demo_profiles.json");
}

TEST_F(RuntimeUtilitiesTest, FallsBackToRelativePathWhenEnvironmentVariableIsMissing) {
  ASSERT_EQ(unsetenv("BUILD_WORKSPACE_DIRECTORY"), 0);

  EXPECT_EQ(
      ResolveWorkspaceRelativePath("tests/testdata/recommender_engine/profile/demo_profiles.json"),
      "tests/testdata/recommender_engine/profile/demo_profiles.json");
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
