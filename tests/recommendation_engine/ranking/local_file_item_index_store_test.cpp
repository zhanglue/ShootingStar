#include "src/recommendation_engine/ranking/local_file_item_index_store.h"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace recommendation_engine {
namespace {

using ::shooting_star::utilities::ResolveWorkspaceRelativePath;
using ::std::optional;
using ::std::string;
using ::std::vector;

class LocalFileItemIndexStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    item_index_data_path_ =
        ResolveWorkspaceRelativePath(kItemIndexDataRelativePath);
  }

  static constexpr const char* kItemIndexDataRelativePath =
      "tests/testdata/recommendation_engine/local_recommendation_fixture/"
      "item_index.jsonl";

  string item_index_data_path_;
};

TEST_F(LocalFileItemIndexStoreTest, LoadsItemEntriesFromJsonlFile) {
  const LocalFileItemIndexStore store(item_index_data_path_);

  optional<ItemIndexEntry> item = store.FindByItemId(1);
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->item_id, 1);
  EXPECT_EQ(item->title, "Toy Story");
  EXPECT_EQ(item->title_raw, "Toy Story (1995)");
  EXPECT_EQ(item->title_norm, "toy story");
  EXPECT_EQ(item->year, 1995);
  EXPECT_EQ(item->genres,
            (vector<string>{"Adventure", "Animation", "Children", "Comedy",
                            "Fantasy"}));
  ASSERT_EQ(item->top_tags.size(), 10);
  EXPECT_EQ(item->top_tags[0].tag, "pixar");
  EXPECT_DOUBLE_EQ(item->top_tags[0].weight, 44.7209);
  EXPECT_EQ(item->tag_count, 1230);
  EXPECT_EQ(item->unique_tag_count, 423);
  EXPECT_DOUBLE_EQ(item->rating.avg, 3.8974);
  EXPECT_EQ(item->rating.count, 68997);
  EXPECT_EQ(
      item->search.tag_text,
      "pixar tom hanks toys computer animation clever witty animation disney "
      "tim allen humorous");
  EXPECT_EQ(item->search.all_text,
            "toy story adventure animation children comedy fantasy pixar tom "
            "hanks toys computer animation clever witty animation disney tim "
            "allen humorous");
  EXPECT_EQ(item->ext.imdb_id, "tt0114709");
  EXPECT_EQ(item->ext.tmdb_id, 862);
}

TEST_F(LocalFileItemIndexStoreTest, LoadsEntriesWithEmptyTopTags) {
  const LocalFileItemIndexStore store(item_index_data_path_);

  optional<ItemIndexEntry> item = store.FindByItemId(90);
  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->item_id, 90);
  EXPECT_EQ(item->title, "The Journey of August King");
  EXPECT_EQ(item->genres, (vector<string>{"Drama"}));
  EXPECT_TRUE(item->top_tags.empty());
  EXPECT_EQ(item->tag_count, 0);
  EXPECT_EQ(item->unique_tag_count, 0);
  EXPECT_DOUBLE_EQ(item->rating.avg, 3.344);
  EXPECT_EQ(item->rating.count, 218);
}

TEST_F(LocalFileItemIndexStoreTest, ReturnsNulloptWhenItemDoesNotExist) {
  const LocalFileItemIndexStore store(item_index_data_path_);

  EXPECT_FALSE(store.FindByItemId(999999).has_value());
}

TEST_F(LocalFileItemIndexStoreTest, ThrowsWhenFileDoesNotExist) {
  EXPECT_THROW(static_cast<void>(LocalFileItemIndexStore(
                   "/tmp/local_file_item_index_store_missing.jsonl")),
               ::std::runtime_error);
}

}  // namespace
}  // namespace recommendation_engine
