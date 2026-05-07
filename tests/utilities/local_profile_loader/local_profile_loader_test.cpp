#include <cstdint>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "protos/recommendation_engine/profile.pb.h"
#include "src/utilities/local_profile_loader/local_profile_loader.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {
namespace {

using ::recommendation_engine::Profile;
using ::std::numeric_limits;
using ::std::string;

class LocalProfileLoaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    profile_data_path_ = ResolveWorkspaceRelativePath(kProfileDataRelativePath);
  }

  static constexpr const char* kProfileDataRelativePath =
      "tests/testdata/recommendation_engine/local_recommendation_fixture/profiles.jsonl";

  string profile_data_path_;
};

TEST_F(LocalProfileLoaderTest, LoadsProfileForExistingUser) {
  Profile profile;
  string error_msg;

  EXPECT_TRUE(
      LoadProfileFromLocalFile(profile_data_path_, 153, &profile, &error_msg));
  EXPECT_TRUE(error_msg.empty());
  EXPECT_EQ(profile.user_id(), 153);
  EXPECT_EQ(profile.demographics().display_name(), "User 153");
  EXPECT_EQ(profile.social().following_size(), 0);
  EXPECT_GT(profile.behaviors().liked_items_size(), 0);
  EXPECT_GT(profile.interests().tags_size(), 0);
  EXPECT_EQ(profile.negative_feedbacks().items_size(), 1);
  EXPECT_EQ(profile.stats().positive_rating_count(), 17);
  EXPECT_EQ(profile.embedding_sets_size(), 0);
}

TEST_F(LocalProfileLoaderTest, ReturnsFalseWhenUserDoesNotExist) {
  Profile profile;
  string error_msg;

  EXPECT_FALSE(
      LoadProfileFromLocalFile(profile_data_path_, 999999, &profile, &error_msg));
  EXPECT_NE(error_msg.find("Cannot find demo profile"), string::npos);
}

TEST_F(LocalProfileLoaderTest, ReturnsFalseWhenProfileOutputIsNull) {
  string error_msg;

  EXPECT_FALSE(LoadProfileFromLocalFile(profile_data_path_, 153, nullptr, &error_msg));
  EXPECT_EQ(error_msg, "profile output pointer must not be null.");
}

TEST_F(LocalProfileLoaderTest, ReturnsFalseWhenUserIdIsOutOfRange) {
  Profile profile;
  string error_msg;

  EXPECT_FALSE(LoadProfileFromLocalFile(
      profile_data_path_,
      static_cast<int64_t>(numeric_limits<int>::max()) + 1,
      &profile,
      &error_msg));
  EXPECT_EQ(error_msg, "user_id is out of range for local JSONL profile lookup.");
}

TEST_F(LocalProfileLoaderTest, ReturnsFalseWhenJsonFileDoesNotExist) {
  Profile profile;
  string error_msg;

  EXPECT_FALSE(LoadProfileFromLocalFile(
      "/tmp/local_profile_loader_missing.json", 153, &profile, &error_msg));
  EXPECT_NE(error_msg.find("cannot open file"), string::npos);
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
