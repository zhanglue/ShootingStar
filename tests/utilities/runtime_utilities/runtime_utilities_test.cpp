#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {
namespace {

using std::getenv;
using std::ofstream;
using std::string;
using std::filesystem::absolute;
using std::filesystem::create_directories;
using std::filesystem::current_path;
using std::filesystem::path;
using std::filesystem::weakly_canonical;

class RuntimeUtilitiesTest : public ::testing::Test {
 protected:
  path GetTestRoot(const string& name) const {
    const char* test_tmpdir = getenv("TEST_TMPDIR");
    EXPECT_NE(test_tmpdir, nullptr);
    if (test_tmpdir == nullptr) {
      return current_path() / name;
    }
    return path(test_tmpdir) / name;
  }

  void CreateFile(const path& file_path) {
    create_directories(file_path.parent_path());
    ofstream fout(file_path);
    ASSERT_TRUE(fout.is_open());
    fout << "{}";
  }
};

TEST_F(RuntimeUtilitiesTest, ReturnsConfiguredPathWhenProvided) {
  EXPECT_EQ(
      ResolveWorkspaceRelativePath("/tmp/profiles.json"),
      "/tmp/profiles.json");
}

TEST_F(RuntimeUtilitiesTest, ResolvesPathRelativeToExecutableDirectoryParents) {
  const path test_root = GetTestRoot("runtime_utilities_executable_root");
  const path executable_path = test_root / "AAA/BBB/CCC/profile_server";
  const path expected_path =
      test_root / "tests/testdata/recommendation_engine/profile/demo_profiles.json";
  CreateFile(expected_path);

  EXPECT_EQ(
      ResolveWorkspaceRelativePath(
          "tests/testdata/recommendation_engine/profile/demo_profiles.json",
          executable_path.string()),
      expected_path.lexically_normal().string());
}

TEST_F(RuntimeUtilitiesTest, FallsBackToExecutableDirectoryWhenNoCandidateExists) {
  EXPECT_EQ(
      ResolveWorkspaceRelativePath(
          "profiles/demo_profiles.json",
          "AAA/BBB/CCC/profile_server"),
      (absolute("AAA/BBB/CCC") / "profiles/demo_profiles.json")
          .lexically_normal()
          .string());
}

TEST_F(RuntimeUtilitiesTest, ResolvesPathRelativeToCurrentDirectoryParents) {
  const path test_root = GetTestRoot("runtime_utilities_current_path_root");
  const path original_cwd = current_path();
  const path nested_cwd = test_root / "DDD/EEE/FFF";
  const path expected_path =
      test_root / "tests/testdata/recommendation_engine/profile/demo_profiles.json";
  CreateFile(expected_path);
  create_directories(nested_cwd);

  current_path(nested_cwd);
  EXPECT_EQ(
      ResolveWorkspaceRelativePath("tests/testdata/recommendation_engine/profile/demo_profiles.json"),
      weakly_canonical(expected_path).string());
  current_path(original_cwd);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
