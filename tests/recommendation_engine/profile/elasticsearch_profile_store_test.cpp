#include "src/recommendation_engine/profile/elasticsearch_profile_store.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/utilities/elasticsearch_client/elasticsearch_client.h"
#include "src/utilities/http_client/http_client.h"

namespace shooting_star::recommendation_engine {
namespace {

using ::shooting_star::utilities::ElasticsearchClient;
using ::shooting_star::utilities::HttpClient;
using ::shooting_star::utilities::HttpErrorCode;
using ::shooting_star::utilities::HttpRequest;
using ::shooting_star::utilities::HttpResponse;
using ::shooting_star::utilities::HttpResult;
using ::std::optional;
using ::std::string;
using ::std::vector;

class FakeHttpClient : public HttpClient {
 public:
  vector<HttpRequest> requests;
  HttpResult next_result;

 private:
  HttpResult Execute(const HttpRequest& request) override {
    requests.push_back(request);
    return next_result;
  }
};

TEST(ElasticsearchProfileStoreTest, FetchesProfileByUserId) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.response.status_code = 200;
  http_client->next_result.response.body =
      R"({"found":true,"_source":{"user_id":42,"demographics":{"username":"user_42"}}})";

  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";
  ElasticsearchProfileStore store(
      ElasticsearchClient::Create(http_client, ::std::move(config)),
      "user_profiles");

  optional<Profile> profile = store.FindByUserId(42);

  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile->user_id(), 42);
  EXPECT_EQ(profile->demographics().username(), "user_42");
  ASSERT_EQ(http_client->requests.size(), 1);
  EXPECT_EQ(http_client->requests.front().url,
            "http://localhost:9200/user_profiles/_doc/42");
}

TEST(ElasticsearchProfileStoreTest, ReturnsNullptrWhenDocumentIsMissing) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.error_code = HttpErrorCode::kHttpStatusError;
  http_client->next_result.error_message = "HTTP request returned status 404";
  http_client->next_result.response.status_code = 404;
  http_client->next_result.response.body = R"({"found":false})";

  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";
  ElasticsearchProfileStore store(
      ElasticsearchClient::Create(http_client, ::std::move(config)),
      "user_profiles");

  EXPECT_FALSE(store.FindByUserId(404).has_value());
}

}  // namespace
}  // namespace shooting_star::recommendation_engine
