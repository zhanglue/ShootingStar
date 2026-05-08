#include "src/recommendation_engine/retrieval/retrievers/user_cf/retriever_user_cf.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace recommendation_engine {
namespace {

using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::optional;
using ::std::pair;
using ::std::runtime_error;
using ::std::unordered_map;
using ::std::vector;

class FakeUserSimilarityStore final : public user_cf::UserSimilarityStore {
 public:
  unordered_map<uint64_t, vector<user_cf::UserNeighbor>> neighbors_by_user_id;
  mutable vector<pair<uint64_t, int>> requests;
  bool should_throw = false;

  vector<user_cf::UserNeighbor> FindNeighborsByUserId(
      uint64_t user_id, int max_neighbor_count) const override {
    requests.emplace_back(user_id, max_neighbor_count);
    if (should_throw) {
      throw runtime_error("redis unavailable");
    }

    const auto iter = neighbors_by_user_id.find(user_id);
    if (iter == neighbors_by_user_id.end()) {
      return {};
    }

    vector<user_cf::UserNeighbor> neighbors;
    for (const user_cf::UserNeighbor& neighbor : iter->second) {
      if (static_cast<int>(neighbors.size()) >= max_neighbor_count) {
        break;
      }
      neighbors.emplace_back(neighbor);
    }
    return neighbors;
  }
};

class FakeProfileStore final : public user_cf::ProfileStore {
 public:
  unordered_map<uint64_t, UserCfProfile> profiles_by_user_id;
  mutable vector<uint64_t> requests;
  bool should_throw = false;

  optional<UserCfProfile> FindByUserId(uint64_t user_id) const override {
    requests.emplace_back(user_id);
    if (should_throw) {
      throw runtime_error("profile unavailable");
    }

    const auto iter = profiles_by_user_id.find(user_id);
    if (iter == profiles_by_user_id.end()) {
      return ::std::nullopt;
    }
    return iter->second;
  }
};

WeightedItem* AddWeightedItem(
    ::google::protobuf::RepeatedPtrField<WeightedItem>* items,
    int64_t item_id, float weight) {
  WeightedItem* item = items->Add();
  item->set_item_id(item_id);
  item->set_weight(weight);
  return item;
}

RetrieverRequest BuildRequest() {
  RetrieverRequest request;
  request.set_request_id("request-1");
  request.set_user_id(42);
  request.set_max_candidate_count(3);

  Profile* profile = request.mutable_profile();
  AddWeightedItem(profile->mutable_behaviors()->mutable_recent_liked_items(),
                  500, 1.0F);
  profile->mutable_behaviors()->add_rated_items(300);
  AddWeightedItem(profile->mutable_negative_feedbacks()->mutable_items(),
                  400, 1.0F);
  return request;
}

UserCfProfile BuildNeighborProfile(
    uint64_t user_id,
    const vector<pair<int64_t, float>>& recent_liked_items,
    const vector<pair<int64_t, float>>& liked_items,
    const vector<pair<int64_t, float>>& interested_items = {}) {
  UserCfProfile profile;
  profile.set_user_id(static_cast<int64_t>(user_id));
  for (const auto& [item_id, weight] : recent_liked_items) {
    AddWeightedItem(profile.mutable_recent_liked_items(), item_id, weight);
  }
  for (const auto& [item_id, weight] : liked_items) {
    AddWeightedItem(profile.mutable_liked_items(), item_id, weight);
  }
  for (const auto& [item_id, weight] : interested_items) {
    AddWeightedItem(profile.mutable_interested_items(), item_id, weight);
  }
  return profile;
}

TEST(RetrieverUserCfTest, RetrievesAndRanksSimilarUserProfileItems) {
  auto fake_similarity_store = ::std::make_unique<FakeUserSimilarityStore>();
  FakeUserSimilarityStore* raw_similarity_store = fake_similarity_store.get();
  fake_similarity_store->neighbors_by_user_id = {
      {42, {{10, 0.9}, {20, 0.5}}},
  };

  auto fake_profile_store = ::std::make_unique<FakeProfileStore>();
  FakeProfileStore* raw_profile_store = fake_profile_store.get();
  fake_profile_store->profiles_by_user_id = {
      {10, BuildNeighborProfile(10, {{101, 2.0F}, {300, 1.0F}},
                                {{102, 1.0F}})},
      {20, BuildNeighborProfile(20, {{101, 1.0F}},
                                {{201, 2.0F}})},
  };

  RetrieverUserCf retriever(::std::move(fake_similarity_store),
                            ::std::move(fake_profile_store),
                            RetrieverUserCf::Options{});
  RetrieverRequest request = BuildRequest();
  RetrieverResponse response;

  const Status status = retriever.Retrieve(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.status(), RetrieverServiceStatus::RETRIEVER_SUCCESS);
  ASSERT_EQ(response.candidates_size(), 3);

  EXPECT_EQ(response.candidates(0).item_id(), 101);
  EXPECT_NEAR(response.candidates(0).retrieve_score(), 2.3, 1e-9);
  ASSERT_EQ(response.candidates(0).reasons_size(), 1);
  EXPECT_EQ(response.candidates(0).reasons(0).reason_type(),
            ReasonType::REASON_TYPE_USER_CF);
  EXPECT_EQ(response.candidates(0).reasons(0).trigger().entity_type(),
            EntityType::ENTITY_TYPE_USER);
  EXPECT_EQ(response.candidates(0).reasons(0).trigger().entity_id(), 10);
  EXPECT_NEAR(response.candidates(0).reasons(0).reason_score(), 1.8, 1e-9);

  EXPECT_EQ(response.candidates(1).item_id(), 201);
  EXPECT_NEAR(response.candidates(1).retrieve_score(), 0.7, 1e-9);
  EXPECT_EQ(response.candidates(2).item_id(), 102);
  EXPECT_NEAR(response.candidates(2).retrieve_score(), 0.63, 1e-9);

  ASSERT_EQ(raw_similarity_store->requests.size(), 1);
  EXPECT_EQ(raw_similarity_store->requests[0], (pair<uint64_t, int>{42, 10}));
  EXPECT_EQ(raw_profile_store->requests, (vector<uint64_t>{10, 20}));
}

TEST(RetrieverUserCfTest, ReturnsSystemErrorWhenSimilarityStoreFails) {
  auto fake_similarity_store = ::std::make_unique<FakeUserSimilarityStore>();
  fake_similarity_store->should_throw = true;
  auto fake_profile_store = ::std::make_unique<FakeProfileStore>();
  RetrieverUserCf retriever(::std::move(fake_similarity_store),
                            ::std::move(fake_profile_store),
                            RetrieverUserCf::Options{});
  RetrieverRequest request = BuildRequest();
  RetrieverResponse response;

  const Status status = retriever.Retrieve(nullptr, &request, &response);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(response.status(), RetrieverServiceStatus::RETRIEVER_SYSTEM_ERROR);
}

TEST(RetrieverUserCfTest, ReturnsSystemErrorWhenProfileStoreFails) {
  auto fake_similarity_store = ::std::make_unique<FakeUserSimilarityStore>();
  fake_similarity_store->neighbors_by_user_id = {
      {42, {{10, 0.9}}},
  };
  auto fake_profile_store = ::std::make_unique<FakeProfileStore>();
  fake_profile_store->should_throw = true;
  RetrieverUserCf retriever(::std::move(fake_similarity_store),
                            ::std::move(fake_profile_store),
                            RetrieverUserCf::Options{});
  RetrieverRequest request = BuildRequest();
  RetrieverResponse response;

  const Status status = retriever.Retrieve(nullptr, &request, &response);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(response.status(), RetrieverServiceStatus::RETRIEVER_SYSTEM_ERROR);
}

TEST(RetrieverUserCfTest, ReturnsEmptyTriggerSeedsWhenNoSimilarUsers) {
  auto fake_similarity_store = ::std::make_unique<FakeUserSimilarityStore>();
  auto fake_profile_store = ::std::make_unique<FakeProfileStore>();
  RetrieverUserCf retriever(::std::move(fake_similarity_store),
                            ::std::move(fake_profile_store),
                            RetrieverUserCf::Options{});
  RetrieverRequest request = BuildRequest();
  RetrieverResponse response;

  const Status status = retriever.Retrieve(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.status(),
            RetrieverServiceStatus::RETRIEVER_EMPTY_TRIGGER_SEEDS);
  EXPECT_EQ(response.candidates_size(), 0);
  EXPECT_EQ(response.candidate_count(), 0);
}

TEST(RetrieverUserCfTest, SkipsMissingSimilarUserProfiles) {
  auto fake_similarity_store = ::std::make_unique<FakeUserSimilarityStore>();
  fake_similarity_store->neighbors_by_user_id = {
      {42, {{10, 0.9}, {20, 0.5}}},
  };
  auto fake_profile_store = ::std::make_unique<FakeProfileStore>();
  fake_profile_store->profiles_by_user_id = {
      {20, BuildNeighborProfile(20, {{101, 1.0F}}, {})},
  };
  RetrieverUserCf retriever(::std::move(fake_similarity_store),
                            ::std::move(fake_profile_store),
                            RetrieverUserCf::Options{});
  RetrieverRequest request = BuildRequest();
  RetrieverResponse response;

  const Status status = retriever.Retrieve(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.status(), RetrieverServiceStatus::RETRIEVER_SUCCESS);
  ASSERT_EQ(response.candidates_size(), 1);
  EXPECT_EQ(response.candidates(0).item_id(), 101);
  EXPECT_NEAR(response.candidates(0).retrieve_score(), 0.5, 1e-9);
  ASSERT_EQ(response.candidates(0).reasons_size(), 1);
  EXPECT_EQ(response.candidates(0).reasons(0).trigger().entity_id(), 20);
}

}  // namespace
}  // namespace recommendation_engine
