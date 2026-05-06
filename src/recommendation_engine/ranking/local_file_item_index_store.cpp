#include "src/recommendation_engine/ranking/local_file_item_index_store.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <google/protobuf/struct.pb.h>

#include "src/utilities/json_parser/json_parser.h"

namespace recommendation_engine {
namespace {

using ::google::protobuf::Value;
using ::shooting_star::utilities::JsonParser;
using ::std::ifstream;
using ::std::optional;
using ::std::runtime_error;
using ::std::string;
using ::std::string_view;

bool IsBlank(string_view value) {
  return value.find_first_not_of(" \t\r\n") == string_view::npos;
}

bool ReadFile(const string& file_path, string* content, string* error_msg) {
  ifstream fin(file_path);
  if (!fin.is_open()) {
    if (error_msg != nullptr) {
      *error_msg =
          "LocalFileItemIndexStore::LoadFromFile failed: cannot open file " +
          file_path;
    }
    return false;
  }

  ::std::ostringstream buffer;
  buffer << fin.rdbuf();
  *content = buffer.str();
  return true;
}

char FirstNonBlankChar(string_view content) {
  const size_t position = content.find_first_not_of(" \t\r\n");
  if (position == string_view::npos) {
    return '\0';
  }
  return content[position];
}

void AddEntry(ItemIndexEntry entry, ItemIdIndexEntryMap* loaded_entries) {
  (*loaded_entries)[entry.item_id] = ::std::move(entry);
}

void ParseJsonObjectText(string_view json,
                         string_view context,
                         ItemIdIndexEntryMap* loaded_entries) {
  AddEntry(ItemIndexStore::ParseEntryFromJsonString(json, context),
           loaded_entries);
}

void ParseJsonlContent(string_view content,
                       ItemIdIndexEntryMap* loaded_entries) {
  ::std::istringstream stream{string(content)};
  string line;
  int line_number = 0;
  while (::std::getline(stream, line)) {
    ++line_number;
    if (IsBlank(line)) {
      continue;
    }
    ParseJsonObjectText(line,
                        "JSONL line " + ::std::to_string(line_number),
                        loaded_entries);
  }
}

void ParseJsonArrayContent(string_view content,
                           ItemIdIndexEntryMap* loaded_entries) {
  const Value root = JsonParser::ParseValue(content, "JSON array file");
  if (root.kind_case() != Value::kListValue) {
    throw runtime_error("Item index JSON file must be an array or JSONL.");
  }

  const auto& values = root.list_value().values();
  for (int i = 0; i < values.size(); ++i) {
    const Value& value = values.Get(i);
    if (value.kind_case() != Value::kStructValue) {
      throw runtime_error(
          "JSON array item " + ::std::to_string(i) + " must be an object.");
    }
    AddEntry(ItemIndexStore::ParseEntryFromJsonObject(
                 value.struct_value(),
                 "JSON array item " + ::std::to_string(i)),
             loaded_entries);
  }
}

}  // namespace

LocalFileItemIndexStore::LocalFileItemIndexStore(const string& file_path) {
  string error_msg;
  if (!LoadFromFile(file_path, &error_msg)) {
    throw runtime_error(error_msg);
  }
}

bool LocalFileItemIndexStore::LoadFromFile(const string& file_path,
                                           string* error_msg) {
  string content;
  if (!ReadFile(file_path, &content, error_msg)) {
    return false;
  }

  try {
    ItemIdIndexEntryMap loaded_entries;
    const char first_char = FirstNonBlankChar(content);
    if (first_char == '\0') {
      entries_.clear();
      return true;
    }
    if (first_char == '[') {
      ParseJsonArrayContent(content, &loaded_entries);
    } else {
      ParseJsonlContent(content, &loaded_entries);
    }
    entries_ = ::std::move(loaded_entries);
    return true;
  } catch (const ::std::exception& ex) {
    if (error_msg != nullptr) {
      *error_msg = "LocalFileItemIndexStore::LoadFromFile failed: " +
                   string(ex.what());
    }
    return false;
  }
}

optional<ItemIndexEntry> LocalFileItemIndexStore::FindByItemId(
    uint64_t item_id) const {
  const auto iter = entries_.find(item_id);
  if (iter == entries_.end()) {
    return ::std::nullopt;
  }
  return iter->second;
}

}  // namespace recommendation_engine
