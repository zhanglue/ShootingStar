#include "src/recommendation_engine/retrieval/retrievers/user_cf/local_file_user_similarity_store.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace recommendation_engine {
namespace {

using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::string;
using ::std::vector;

class LocalFileUserSimilarityStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    data_path_ = ResolveWorkspaceRelativePath(
        "tests/recommendation_engine/retrieval/retrievers/user_cf/"
        "local_file_user_similarity_store_testdata.jsonl");
  }

  string data_path_;
};

TEST_F(LocalFileUserSimilarityStoreTest, LoadsNeighborsFromJsonlFile) {
  const LocalFileUserSimilarityStore store(data_path_);

  const vector<UserNeighbor> neighbors = store.FindNeighborsByUserId(42, 2);

  ASSERT_EQ(neighbors.size(), 2);
  EXPECT_EQ(neighbors[0].user_id, 10);
  EXPECT_DOUBLE_EQ(neighbors[0].score, 0.9);
  EXPECT_EQ(neighbors[1].user_id, 20);
  EXPECT_DOUBLE_EQ(neighbors[1].score, 0.5);
}

TEST_F(LocalFileUserSimilarityStoreTest, LimitsNeighborCount) {
  const LocalFileUserSimilarityStore store(data_path_);

  const vector<UserNeighbor> neighbors = store.FindNeighborsByUserId(42, 1);

  ASSERT_EQ(neighbors.size(), 1);
  EXPECT_EQ(neighbors[0].user_id, 10);
}

TEST_F(LocalFileUserSimilarityStoreTest, ReturnsEmptyForMissingOrInvalidLookup) {
  const LocalFileUserSimilarityStore store(data_path_);

  EXPECT_TRUE(store.FindNeighborsByUserId(999999, 2).empty());
  EXPECT_TRUE(store.FindNeighborsByUserId(42, 0).empty());
  EXPECT_TRUE(store.FindNeighborsByUserId(0, 2).empty());
}

TEST_F(LocalFileUserSimilarityStoreTest, ThrowsWhenJsonlFileDoesNotExist) {
  EXPECT_THROW(
      static_cast<void>(LocalFileUserSimilarityStore("/tmp/missing_user_similarity.jsonl")),
      ::std::runtime_error);
}

}  // namespace
}  // namespace recommendation_engine
