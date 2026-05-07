#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "src/recommendation_engine/ranking/item_index_store.h"

namespace recommendation_engine {

using ItemIdIndexEntryMap = ::std::unordered_map<uint64_t, ItemIndexEntry>;

class LocalFileItemIndexStore final : public ItemIndexStore {
 public:
  explicit LocalFileItemIndexStore(const ::std::string& file_path);

  ::std::optional<ItemIndexEntry> FindByItemId(uint64_t item_id) const override;

 private:
  bool LoadFromFile(const ::std::string& file_path,
                    ::std::string* error_msg = nullptr);

  ItemIdIndexEntryMap entries_;
};

}  // namespace recommendation_engine
