#include "src/recommendation_engine/ranking/elasticsearch_item_index_store.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/utilities/elasticsearch_client/elasticsearch_client.h"
#include "src/utilities/http_client/http_client.h"

namespace recommendation_engine {
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

TEST(ElasticsearchItemIndexStoreTest, FetchesItemByItemId) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.response.status_code = 200;
  http_client->next_result.response.body =
      R"({"found":true,"_source":{"item_id":1,"title":"Toy Story","year":1995,"genres":["Adventure","Animation"],"tags":{"top_tags":[{"tag":"pixar","weight":12.3}],"tag_count":42,"unique_tag_count":15},"rating":{"avg":4.3,"count":8},"search":{"tag_text":"pixar","all_text":"toy story pixar"},"ext":{"imdb_id":"tt0114709","tmdb_id":862}}})";

  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";
  ElasticsearchItemIndexStore store(
      ElasticsearchClient::Create(http_client, ::std::move(config)),
      "item_index");

  optional<ItemIndexEntry> item = store.FindByItemId(1);

  ASSERT_TRUE(item.has_value());
  EXPECT_EQ(item->item_id, 1);
  EXPECT_EQ(item->title, "Toy Story");
  EXPECT_EQ(item->year, 1995);
  EXPECT_EQ(item->genres, (vector<string>{"Adventure", "Animation"}));
  ASSERT_EQ(item->top_tags.size(), 1);
  EXPECT_EQ(item->top_tags[0].tag, "pixar");
  EXPECT_DOUBLE_EQ(item->top_tags[0].weight, 12.3);
  EXPECT_EQ(item->tag_count, 42);
  EXPECT_EQ(item->unique_tag_count, 15);
  EXPECT_DOUBLE_EQ(item->rating.avg, 4.3);
  EXPECT_EQ(item->rating.count, 8);
  EXPECT_EQ(item->search.all_text, "toy story pixar");
  EXPECT_EQ(item->ext.imdb_id, "tt0114709");
  EXPECT_EQ(item->ext.tmdb_id, 862);

  ASSERT_EQ(http_client->requests.size(), 1);
  EXPECT_EQ(http_client->requests.front().url,
            "http://localhost:9200/item_index/_doc/1");
}

TEST(ElasticsearchItemIndexStoreTest, ReturnsNulloptWhenDocumentIsMissing) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.error_code = HttpErrorCode::kHttpStatusError;
  http_client->next_result.error_message = "HTTP request returned status 404";
  http_client->next_result.response.status_code = 404;
  http_client->next_result.response.body = R"({"found":false})";

  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";
  ElasticsearchItemIndexStore store(
      ElasticsearchClient::Create(http_client, ::std::move(config)),
      "item_index");

  EXPECT_FALSE(store.FindByItemId(404).has_value());
}

TEST(ElasticsearchItemIndexStoreTest, ReturnsNulloptWhenFoundIsFalse) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.response.status_code = 200;
  http_client->next_result.response.body = R"({"found":false})";

  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";
  ElasticsearchItemIndexStore store(
      ElasticsearchClient::Create(http_client, ::std::move(config)),
      "item_index");

  EXPECT_FALSE(store.FindByItemId(404).has_value());
}

}  // namespace
}  // namespace recommendation_engine
