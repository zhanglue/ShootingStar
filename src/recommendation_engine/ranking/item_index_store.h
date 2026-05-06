#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <google/protobuf/struct.pb.h>

namespace recommendation_engine {

struct ItemIndexWeightedTag {
  ::std::string tag;
  double weight = 0.0;
};

struct ItemIndexRating {
  double avg = 0.0;
  int count = 0;
};

struct ItemIndexSearchFields {
  ::std::string tag_text;
  ::std::string all_text;
};

struct ItemIndexExternalIds {
  ::std::string imdb_id;
  int64_t tmdb_id = 0;
};

struct ItemIndexEntry {
  uint64_t item_id = 0;
  ::std::string title;
  ::std::string title_raw;
  ::std::string title_norm;
  int year = 0;
  ::std::vector<::std::string> genres;
  ::std::vector<ItemIndexWeightedTag> top_tags;
  int tag_count = 0;
  int unique_tag_count = 0;
  ItemIndexRating rating;
  ItemIndexSearchFields search;
  ItemIndexExternalIds ext;
};

class ItemIndexStore {
 public:
  static constexpr ::std::string_view kLocalStoreType = "local";
  static constexpr ::std::string_view kElasticsearchStoreType =
      "elasticsearch";

  virtual ~ItemIndexStore() = default;

  static ItemIndexEntry ParseEntryFromJsonString(
      ::std::string_view item_index_json,
      ::std::string_view context);
  static ItemIndexEntry ParseEntryFromJsonObject(
      const ::google::protobuf::Struct& item_index_object,
      ::std::string_view context);

  virtual ::std::optional<ItemIndexEntry> FindByItemId(
      uint64_t item_id) const = 0;

  virtual ::std::vector<::std::optional<ItemIndexEntry>> FindByItemIds(
      const ::std::vector<uint64_t>& item_ids) const {
    ::std::vector<::std::optional<ItemIndexEntry>> entries_by_item_id;
    entries_by_item_id.reserve(item_ids.size());
    for (const uint64_t item_id : item_ids) {
      entries_by_item_id.emplace_back(FindByItemId(item_id));
    }
    return entries_by_item_id;
  }
};

}  // namespace recommendation_engine
