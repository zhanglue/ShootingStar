#include "src/recommendation_engine/ranking/item_index_store.h"

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "src/utilities/json_parser/json_parser.h"

namespace recommendation_engine {
namespace {

using ::google::protobuf::Map;
using ::google::protobuf::Value;
using ::shooting_star::utilities::JsonParser;
using ::std::format;
using ::std::runtime_error;
using ::std::string;
using ::std::string_view;
using ::std::vector;

vector<ItemIndexWeightedTag> ReadOptionalTopTags(
    const Map<string, Value>& fields,
    string_view context) {
  const Value* value = JsonParser::FindField(fields, "top_tags");
  if (value == nullptr || JsonParser::IsNull(*value)) {
    return {};
  }
  if (value->kind_case() != Value::kListValue) {
    throw runtime_error(format("{}.top_tags must be an array", context));
  }

  const auto& list = value->list_value();
  vector<ItemIndexWeightedTag> top_tags;
  top_tags.reserve(list.values_size());
  for (int i = 0; i < list.values_size(); ++i) {
    const Value& tag_value = list.values(i);
    if (tag_value.kind_case() != Value::kStructValue) {
      throw runtime_error(format("{}.top_tags[{}] must be an object",
                                 context,
                                 i));
    }

    const auto& tag_fields = tag_value.struct_value().fields();
    top_tags.emplace_back(ItemIndexWeightedTag{
        .tag = JsonParser::ReadOptionalString(
            tag_fields,
            "tag",
            format("{}.top_tags[{}]", context, i)),
        .weight = JsonParser::ReadOptionalDouble(
            tag_fields,
            "weight",
            format("{}.top_tags[{}]", context, i)),
    });
  }
  return top_tags;
}

}  // namespace

ItemIndexEntry ItemIndexStore::ParseEntryFromJsonString(
    string_view item_index_json,
    string_view context) {
  return ParseEntryFromJsonObject(
      JsonParser::ParseObject(item_index_json, context),
      context);
}

ItemIndexEntry ItemIndexStore::ParseEntryFromJsonObject(
    const ::google::protobuf::Struct& item_index_object,
    string_view context) {
  const auto& fields = item_index_object.fields();
  ItemIndexEntry entry;
  entry.item_id = JsonParser::ReadRequiredUint64(fields, "item_id", context);
  if (entry.item_id == 0) {
    throw runtime_error(format("{}.item_id must be positive", context));
  }

  entry.title = JsonParser::ReadOptionalString(fields, "title", context);
  entry.title_raw = JsonParser::ReadOptionalString(fields, "title_raw", context);
  entry.title_norm =
      JsonParser::ReadOptionalString(fields, "title_norm", context);
  entry.year = JsonParser::ReadOptionalInt(fields, "year", context);
  entry.genres = JsonParser::ReadOptionalStringList(fields, "genres", context);

  if (const auto* tags = JsonParser::ReadOptionalStruct(fields, "tags", context);
      tags != nullptr) {
    const auto& tag_fields = tags->fields();
    const string tags_context = string(context) + ".tags";
    entry.top_tags = ReadOptionalTopTags(tag_fields, tags_context);
    entry.tag_count = JsonParser::ReadOptionalInt(tag_fields,
                                                  "tag_count",
                                                  tags_context);
    entry.unique_tag_count = JsonParser::ReadOptionalInt(tag_fields,
                                                         "unique_tag_count",
                                                         tags_context);
  }

  if (const auto* rating = JsonParser::ReadOptionalStruct(fields,
                                                          "rating",
                                                          context);
      rating != nullptr) {
    const auto& rating_fields = rating->fields();
    const string rating_context = string(context) + ".rating";
    entry.rating.avg = JsonParser::ReadOptionalDouble(rating_fields,
                                                      "avg",
                                                      rating_context);
    entry.rating.count = JsonParser::ReadOptionalInt(rating_fields,
                                                     "count",
                                                     rating_context);
  }

  if (const auto* search = JsonParser::ReadOptionalStruct(fields,
                                                          "search",
                                                          context);
      search != nullptr) {
    const auto& search_fields = search->fields();
    const string search_context = string(context) + ".search";
    entry.search.tag_text = JsonParser::ReadOptionalString(search_fields,
                                                           "tag_text",
                                                           search_context);
    entry.search.all_text = JsonParser::ReadOptionalString(search_fields,
                                                           "all_text",
                                                           search_context);
  }

  if (const auto* ext = JsonParser::ReadOptionalStruct(fields, "ext", context);
      ext != nullptr) {
    const auto& ext_fields = ext->fields();
    const string ext_context = string(context) + ".ext";
    entry.ext.imdb_id = JsonParser::ReadOptionalString(ext_fields,
                                                       "imdb_id",
                                                       ext_context);
    entry.ext.tmdb_id = JsonParser::ReadOptionalInt64(ext_fields,
                                                      "tmdb_id",
                                                      ext_context);
  }

  return entry;
}

}  // namespace recommendation_engine
