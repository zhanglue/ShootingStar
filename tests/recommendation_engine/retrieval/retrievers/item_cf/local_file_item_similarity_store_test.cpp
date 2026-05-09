#include "src/recommendation_engine/retrieval/retrievers/item_cf/local_file_item_similarity_store.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star::recommendation_engine {
namespace {

using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::string;
using ::std::vector;

class LocalFileItemSimilarityStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    data_path_ = ResolveWorkspaceRelativePath(
        "tests/recommendation_engine/retrieval/retrievers/item_cf/"
        "local_file_item_similarity_store_testdata.jsonl");
  }

  string data_path_;
};

TEST_F(LocalFileItemSimilarityStoreTest, LoadsNeighborsFromJsonlFile) {
  const LocalFileItemSimilarityStore store(data_path_);

  const vector<ItemNeighbor> neighbors = store.FindNeighborsByItemId(100, 2);

  ASSERT_EQ(neighbors.size(), 2);
  EXPECT_EQ(neighbors[0].item_id, 101);
  EXPECT_DOUBLE_EQ(neighbors[0].score, 0.9);
  EXPECT_EQ(neighbors[1].item_id, 102);
  EXPECT_DOUBLE_EQ(neighbors[1].score, 0.7);
}

TEST_F(LocalFileItemSimilarityStoreTest, LimitsNeighborCount) {
  const LocalFileItemSimilarityStore store(data_path_);

  const vector<ItemNeighbor> neighbors = store.FindNeighborsByItemId(100, 1);

  ASSERT_EQ(neighbors.size(), 1);
  EXPECT_EQ(neighbors[0].item_id, 101);
}

TEST_F(LocalFileItemSimilarityStoreTest, ReturnsEmptyForMissingOrInvalidLookup) {
  const LocalFileItemSimilarityStore store(data_path_);

  EXPECT_TRUE(store.FindNeighborsByItemId(999999, 2).empty());
  EXPECT_TRUE(store.FindNeighborsByItemId(100, 0).empty());
  EXPECT_TRUE(store.FindNeighborsByItemId(0, 2).empty());
}

TEST_F(LocalFileItemSimilarityStoreTest, ThrowsWhenJsonlFileDoesNotExist) {
  EXPECT_THROW(
      static_cast<void>(LocalFileItemSimilarityStore("/tmp/missing_item_similarity.jsonl")),
      ::std::runtime_error);
}

}  // namespace
}  // namespace shooting_star::recommendation_engine
