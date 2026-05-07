#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "src/recommendation_engine/ranking/item_index_store.h"
#include "src/utilities/elasticsearch_client/elasticsearch_client.h"

namespace recommendation_engine {

class ElasticsearchItemIndexStore final : public ItemIndexStore {
 public:
  ElasticsearchItemIndexStore(
      ::shooting_star::utilities::ElasticsearchClient client,
      ::std::string index);

  ::std::optional<ItemIndexEntry> FindByItemId(uint64_t item_id) const override;

 private:
  ::std::optional<ItemIndexEntry> ParseItemFromGetResponse(
      const ::std::string& response_body) const;

  ::shooting_star::utilities::ElasticsearchClient client_;
  ::std::string index_;
};

}  // namespace recommendation_engine
