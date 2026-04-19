#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "src/recommendation_engine/profile/local_file_profile_store.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace recommendation_engine {
namespace {

using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::string;

class LocalFileProfileStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    profile_data_path_ = ResolveWorkspaceRelativePath(kProfileDataRelativePath);
  }

  static constexpr const char* kProfileDataRelativePath =
      "tests/testdata/recommendation_engine/profile/demo_profiles.json";

  string profile_data_path_;
};

TEST_F(LocalFileProfileStoreTest, LoadsProfilesFromJsonFile) {
  const LocalFileProfileStore store(profile_data_path_);

  const auto* profile = store.FindByUserId(1002);
  ASSERT_NE(profile, nullptr);
  EXPECT_EQ(profile->user_id(), 1002);
  EXPECT_EQ(profile->demographics().username(), "user_1002");
  EXPECT_EQ(profile->demographics().location_id(), 2);
  EXPECT_EQ(profile->social().following_size(), 1);
  EXPECT_EQ(profile->behaviors().positive_items_size(), 1);
  EXPECT_EQ(profile->interests().tags_size(), 2);
  EXPECT_EQ(profile->negative_feedbacks().items_size(), 1);
  EXPECT_EQ(profile->stats().last_event_time(), 1703718800);
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
}  // namespace recommendation_engine
