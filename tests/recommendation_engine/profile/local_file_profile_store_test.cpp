#include "src/recommendation_engine/profile/local_file_profile_store.h"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star::recommendation_engine {
namespace {

using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::optional;
using ::std::string;

class LocalFileProfileStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    profile_data_path_ = ResolveWorkspaceRelativePath(kProfileDataRelativePath);
    profile_jsonl_data_path_ =
        ResolveWorkspaceRelativePath(kProfileJsonlDataRelativePath);
  }

  static constexpr const char* kProfileDataRelativePath =
      "tests/testdata/recommendation_engine/local_recommendation_fixture/profiles.jsonl";
  static constexpr const char* kProfileJsonlDataRelativePath =
      "tests/recommendation_engine/profile/"
      "local_file_profile_store_testdata.jsonl";

  string profile_data_path_;
  string profile_jsonl_data_path_;
};

TEST_F(LocalFileProfileStoreTest, LoadsDemoProfilesFromJsonlFile) {
  const LocalFileProfileStore store(profile_data_path_);

  optional<Profile> profile = store.FindByUserId(153);
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile->user_id(), 153);
  EXPECT_EQ(profile->demographics().username(), "user_153");
  EXPECT_EQ(profile->demographics().display_name(), "User 153");
  EXPECT_EQ(profile->social().following_size(), 0);
  EXPECT_GT(profile->behaviors().liked_items_size(), 0);
  EXPECT_GT(profile->interests().tags_size(), 0);
  EXPECT_EQ(profile->negative_feedbacks().items_size(), 1);
  EXPECT_EQ(profile->stats().last_event_time(), 861908482);
}

TEST_F(LocalFileProfileStoreTest, LoadsProfileStoreTestdataFromJsonlFile) {
  const LocalFileProfileStore store(profile_jsonl_data_path_);

  optional<Profile> profile = store.FindByUserId(2001);
  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile->user_id(), 2001);
  EXPECT_EQ(profile->demographics().username(), "jsonl_user_2001");
  ASSERT_EQ(profile->behaviors().liked_items_size(), 1);
  EXPECT_EQ(profile->behaviors().liked_items(0).item_id(), 9001);
}

TEST_F(LocalFileProfileStoreTest, ReturnsNullptrWhenUserIdDoesNotExist) {
  const LocalFileProfileStore store(profile_data_path_);

  EXPECT_FALSE(store.FindByUserId(999999).has_value());
}

TEST_F(LocalFileProfileStoreTest, ThrowsWhenJsonlFileDoesNotExist) {
  EXPECT_THROW(static_cast<void>(LocalFileProfileStore(
                   "/tmp/local_file_profile_store_missing.jsonl")),
               ::std::runtime_error);
}

}  // namespace
}  // namespace shooting_star::recommendation_engine
