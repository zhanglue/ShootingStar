#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "src/recommender_engine/profile/local_file_profile_store.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace recommender_engine {
namespace {

using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::string;

class LocalFileProfileStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    profile_data_path_ = ResolveWorkspaceRelativePath(kProfileDataRelativePath);
  }

  static constexpr const char* kProfileDataRelativePath =
      "tests/testdata/recommender_engine/profile/demo_profiles.json";

  string profile_data_path_;
};

TEST_F(LocalFileProfileStoreTest, LoadsProfilesFromJsonFile) {
  const LocalFileProfileStore store(profile_data_path_);

  const auto* profile = store.FindByUserId(1002);
  ASSERT_NE(profile, nullptr);
  EXPECT_EQ(profile->user_id(), 1002);
  EXPECT_EQ(profile->demographics().location_id(), 2);
  EXPECT_EQ(profile->interests().tag_ids_size(), 2);
  EXPECT_EQ(profile->session().recent_clicked_items_size(), 1);
}

TEST_F(LocalFileProfileStoreTest, ReturnsNullptrWhenUserIdDoesNotExist) {
  const LocalFileProfileStore store(profile_data_path_);

  EXPECT_EQ(store.FindByUserId(999999), nullptr);
}

TEST_F(LocalFileProfileStoreTest, ThrowsWhenJsonFileDoesNotExist) {
  EXPECT_THROW(
      static_cast<void>(LocalFileProfileStore("/tmp/local_file_profile_store_missing.json")),
      ::std::runtime_error);
}

}  // namespace
}  // namespace recommender_engine
