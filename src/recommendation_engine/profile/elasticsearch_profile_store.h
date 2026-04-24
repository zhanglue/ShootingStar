#pragma once

#include <optional>
#include <string>

#include "src/recommendation_engine/profile/profile_store.h"
#include "src/utilities/elasticsearch_client/elasticsearch_client.h"

namespace recommendation_engine {

class ElasticsearchProfileStore final : public ProfileStore {
 public:
  ElasticsearchProfileStore(
      ::shooting_star::utilities::ElasticsearchClient client,
      ::std::string index);

  ::std::optional<Profile> FindByUserId(int user_id) const override;

 private:
  bool ParseProfileFromGetResponse(const ::std::string& response_body,
                                   Profile* profile) const;

  ::shooting_star::utilities::ElasticsearchClient client_;
  ::std::string index_;
};

}  // namespace recommendation_engine
