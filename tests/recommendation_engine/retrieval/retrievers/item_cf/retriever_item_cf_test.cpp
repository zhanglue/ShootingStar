#include "src/recommendation_engine/retrieval/retrievers/item_cf/retriever_item_cf.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace recommendation_engine {
namespace {

using ::grpc::Status;
using ::grpc::StatusCode;
using ::std::pair;
using ::std::runtime_error;
using ::std::unordered_map;
using ::std::vector;

class FakeItemSimilarityStore final : public ItemSimilarityStore {
 public:
  unordered_map<uint64_t, vector<ItemNeighbor>> neighbors_by_item_id;
  mutable vector<pair<uint64_t, int>> requests;
  bool should_throw = false;

  vector<ItemNeighbor> FindNeighborsByItemId(
      uint64_t item_id, int max_neighbor_count) const override {
    requests.emplace_back(item_id, max_neighbor_count);
    if (should_throw) {
      throw runtime_error("redis unavailable");
    }

    const auto iter = neighbors_by_item_id.find(item_id);
    if (iter == neighbors_by_item_id.end()) {
      return {};
    }

    vector<ItemNeighbor> neighbors;
    for (const ItemNeighbor& neighbor : iter->second) {
      if (static_cast<int>(neighbors.size()) >= max_neighbor_count) {
        break;
      }
      neighbors.push_back(neighbor);
    }
    return neighbors;
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
                  100, 2.0F);
  AddWeightedItem(profile->mutable_behaviors()->mutable_liked_items(), 200,
                  1.0F);
  profile->mutable_behaviors()->add_rated_items(300);
  AddWeightedItem(profile->mutable_negative_feedbacks()->mutable_items(), 400,
                  1.0F);
  return request;
}

TEST(RetrieverItemCfTest, RetrievesAndRanksRedisItemSimilarityNeighbors) {
  auto fake_store = ::std::make_unique<FakeItemSimilarityStore>();
  FakeItemSimilarityStore* raw_store = fake_store.get();
  fake_store->neighbors_by_item_id = {
      {100, {{101, 0.9}, {300, 0.8}, {102, 0.4}}},
      {200, {{101, 0.8}, {201, 0.7}, {400, 0.6}}},
  };
  RetrieverItemCf retriever(::std::move(fake_store));
  RetrieverRequest request = BuildRequest();
  RetrieverResponse response;

  const Status status = retriever.Retrieve(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.status(), RetrieverServiceStatus::RETRIEVER_SUCCESS);
  ASSERT_EQ(response.candidates_size(), 3);

  EXPECT_EQ(response.candidates(0).item_id(), 101);
  EXPECT_DOUBLE_EQ(response.candidates(0).retrieve_score(), 2.2);
  ASSERT_EQ(response.candidates(0).reasons_size(), 1);
  EXPECT_EQ(response.candidates(0).reasons(0).trigger().entity_id(), 100);
  EXPECT_DOUBLE_EQ(response.candidates(0).reasons(0).reason_score(), 1.8);

  EXPECT_EQ(response.candidates(1).item_id(), 102);
  EXPECT_DOUBLE_EQ(response.candidates(1).retrieve_score(), 0.8);
  EXPECT_EQ(response.candidates(2).item_id(), 201);
  EXPECT_DOUBLE_EQ(response.candidates(2).retrieve_score(), 0.35);

  ASSERT_EQ(raw_store->requests.size(), 2);
  EXPECT_EQ(raw_store->requests[0], (pair<uint64_t, int>{100, 3}));
  EXPECT_EQ(raw_store->requests[1], (pair<uint64_t, int>{200, 3}));
}

TEST(RetrieverItemCfTest, ReturnsSystemErrorWhenSimilarityStoreFails) {
  auto fake_store = ::std::make_unique<FakeItemSimilarityStore>();
  fake_store->should_throw = true;
  RetrieverItemCf retriever(::std::move(fake_store));
  RetrieverRequest request = BuildRequest();
  RetrieverResponse response;

  const Status status = retriever.Retrieve(nullptr, &request, &response);

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(response.status(), RetrieverServiceStatus::RETRIEVER_SYSTEM_ERROR);
}

TEST(RetrieverItemCfTest, ReturnsEmptyTriggerSeedsWhenNoValidTriggerItems) {
  auto fake_store = ::std::make_unique<FakeItemSimilarityStore>();
  RetrieverItemCf retriever(::std::move(fake_store));

  RetrieverRequest request;
  request.set_request_id("request-empty");
  request.set_user_id(42);
  request.set_max_candidate_count(3);
  request.mutable_profile();

  RetrieverResponse response;
  const Status status = retriever.Retrieve(nullptr, &request, &response);

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.status(),
            RetrieverServiceStatus::RETRIEVER_EMPTY_TRIGGER_SEEDS);
  EXPECT_EQ(response.candidates_size(), 0);
  EXPECT_EQ(response.candidate_count(), 0);
}

}  // namespace
}  // namespace recommendation_engine
