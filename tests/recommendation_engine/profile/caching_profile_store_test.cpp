#include "src/recommendation_engine/profile/caching_profile_store.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>

namespace recommendation_engine {
namespace {

using ::std::chrono::milliseconds;
using ::std::optional;

class FakeProfileStore final : public ProfileStore {
 public:
  explicit FakeProfileStore(Profile profile) {
    profiles_[profile.user_id()] = ::std::move(profile);
  }

  optional<Profile> FindByUserId(int user_id) const override {
    ++lookup_count;
    const auto iter = profiles_.find(user_id);
    if (iter == profiles_.end()) {
      return ::std::nullopt;
    }
    return iter->second;
  }

  mutable int lookup_count = 0;

 private:
  ::std::unordered_map<int, Profile> profiles_;
};

Profile CreateProfile(int user_id) {
  Profile profile;
  profile.set_user_id(user_id);
  return profile;
}

TEST(CachingProfileStoreTest, ReusesCachedProfileBeforeTtlExpires) {
  auto inner_store = ::std::make_unique<FakeProfileStore>(CreateProfile(42));
  FakeProfileStore* inner_store_ptr = inner_store.get();
  CachingProfileStore store(::std::move(inner_store), 30, milliseconds(1000));

  ASSERT_TRUE(store.FindByUserId(42).has_value());
  ASSERT_TRUE(store.FindByUserId(42).has_value());

  EXPECT_EQ(inner_store_ptr->lookup_count, 1);
}

TEST(CachingProfileStoreTest, RefetchesProfileAfterTtlExpires) {
  auto inner_store = ::std::make_unique<FakeProfileStore>(CreateProfile(42));
  FakeProfileStore* inner_store_ptr = inner_store.get();
  CachingProfileStore store(::std::move(inner_store), 30, milliseconds(1));

  ASSERT_TRUE(store.FindByUserId(42).has_value());
  ::std::this_thread::sleep_for(milliseconds(2));
  ASSERT_TRUE(store.FindByUserId(42).has_value());

  EXPECT_EQ(inner_store_ptr->lookup_count, 2);
}

}  // namespace
}  // namespace recommendation_engine
