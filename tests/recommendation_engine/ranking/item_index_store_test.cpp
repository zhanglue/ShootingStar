#include "src/recommendation_engine/ranking/item_index_store.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace recommendation_engine {
namespace {

using ::std::optional;
using ::std::string;
using ::std::unordered_map;
using ::std::vector;

ItemIndexEntry BuildEntry(uint64_t item_id, string title) {
  ItemIndexEntry entry;
  entry.item_id = item_id;
  entry.title = ::std::move(title);
  entry.title_raw = entry.title + " (1995)";
  entry.title_norm = "normalized " + entry.title;
  entry.year = 1995;
  entry.genres = {"Adventure", "Comedy"};
  entry.top_tags = {
      ItemIndexWeightedTag{.tag = "friendship", .weight = 3.5},
      ItemIndexWeightedTag{.tag = "classic", .weight = 2.0},
  };
  entry.tag_count = 12;
  entry.unique_tag_count = 7;
  entry.rating = ItemIndexRating{.avg = 4.25, .count = 100};
  entry.search = ItemIndexSearchFields{
      .tag_text = "friendship classic",
      .all_text = "movie friendship classic adventure comedy",
  };
  entry.ext = ItemIndexExternalIds{.imdb_id = "tt0000001", .tmdb_id = 1};
  return entry;
}

class FakeItemIndexStore final : public ItemIndexStore {
 public:
  optional<ItemIndexEntry> FindByItemId(uint64_t item_id) const override {
    requested_item_ids.emplace_back(item_id);
    const auto iter = entries_by_item_id.find(item_id);
    if (iter == entries_by_item_id.end()) {
      return ::std::nullopt;
    }
    return iter->second;
  }

  unordered_map<uint64_t, ItemIndexEntry> entries_by_item_id;
  mutable vector<uint64_t> requested_item_ids;
};

TEST(ItemIndexStoreTest, BatchLookupPreservesOrderAndMissingItems) {
  FakeItemIndexStore store;
  store.entries_by_item_id.emplace(10, BuildEntry(10, "Toy Story"));
  store.entries_by_item_id.emplace(30, BuildEntry(30, "Apollo 13"));

  const vector<optional<ItemIndexEntry>> results =
      store.FindByItemIds({10, 20, 30});

  ASSERT_EQ(results.size(), 3);
  ASSERT_TRUE(results[0].has_value());
  EXPECT_EQ(results[0]->item_id, 10);
  EXPECT_EQ(results[0]->title, "Toy Story");
  EXPECT_EQ(results[0]->genres, (vector<string>{"Adventure", "Comedy"}));
  ASSERT_EQ(results[0]->top_tags.size(), 2);
  EXPECT_EQ(results[0]->top_tags[0].tag, "friendship");
  EXPECT_DOUBLE_EQ(results[0]->top_tags[0].weight, 3.5);
  EXPECT_DOUBLE_EQ(results[0]->rating.avg, 4.25);
  EXPECT_EQ(results[0]->rating.count, 100);
  EXPECT_EQ(results[0]->search.tag_text, "friendship classic");
  EXPECT_EQ(results[0]->ext.imdb_id, "tt0000001");
  EXPECT_FALSE(results[1].has_value());
  ASSERT_TRUE(results[2].has_value());
  EXPECT_EQ(results[2]->item_id, 30);
  EXPECT_EQ(results[2]->title, "Apollo 13");
  EXPECT_EQ(store.requested_item_ids, (vector<uint64_t>{10, 20, 30}));
}

TEST(ItemIndexStoreTest, ParsesItemIndexEntryFromJsonString) {
  const ItemIndexEntry entry = ItemIndexStore::ParseEntryFromJsonString(
      R"({"item_id":7,"title":"Sabrina","genres":["Comedy","Romance"],"tags":{"top_tags":[{"tag":"remake","weight":4.5}]},"rating":{"avg":3.5,"count":9}})",
      "test item");

  EXPECT_EQ(entry.item_id, 7);
  EXPECT_EQ(entry.title, "Sabrina");
  EXPECT_EQ(entry.genres, (vector<string>{"Comedy", "Romance"}));
  ASSERT_EQ(entry.top_tags.size(), 1);
  EXPECT_EQ(entry.top_tags[0].tag, "remake");
  EXPECT_DOUBLE_EQ(entry.top_tags[0].weight, 4.5);
  EXPECT_DOUBLE_EQ(entry.rating.avg, 3.5);
  EXPECT_EQ(entry.rating.count, 9);
}

}  // namespace
}  // namespace recommendation_engine
