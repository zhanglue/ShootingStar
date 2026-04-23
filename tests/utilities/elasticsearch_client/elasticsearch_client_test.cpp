#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/utilities/elasticsearch_client/elasticsearch_client.h"
#include "src/utilities/http_client/http_client.h"

namespace shooting_star {
namespace utilities {
namespace {

using ::std::chrono::milliseconds;
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

::std::optional<string> FindHeader(const vector<HttpHeader>& headers,
                                   const string& name) {
  for (const HttpHeader& header : headers) {
    if (header.name == name) {
      return header.value;
    }
  }
  return ::std::nullopt;
}

TEST(ElasticsearchClientTest, SearchPostsJsonToIndexSearchEndpoint) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.response.status_code = 200;
  http_client->next_result.response.body = R"({"hits":{"hits":[]}})";
  ElasticsearchClient::Config config;
  config.base_url = "https://es.example.com/";
  config.username = "elastic";
  config.password = "secret";
  config.request_timeout = milliseconds(1234);

  ElasticsearchClient client =
      ElasticsearchClient::Create(http_client, ::std::move(config));

  const ElasticsearchResult result =
      client.Search("movies", R"({"query":{"match_all":{}}})");

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.status_code, 200);
  EXPECT_EQ(result.body, R"({"hits":{"hits":[]}})");

  ASSERT_EQ(http_client->requests.size(), 1);
  const HttpRequest& request = http_client->requests.front();
  EXPECT_EQ(request.method, HttpMethod::kPost);
  EXPECT_EQ(request.url, "https://es.example.com/movies/_search");
  EXPECT_EQ(request.body, R"({"query":{"match_all":{}}})");
  EXPECT_EQ(request.timeout, milliseconds(1234));
  EXPECT_EQ(FindHeader(request.headers, "Accept"), "application/json");
  EXPECT_EQ(FindHeader(request.headers, "Content-Type"), "application/json");
  EXPECT_EQ(FindHeader(request.headers, "Authorization"), "Basic ZWxhc3RpYzpzZWNyZXQ=");
}

TEST(ElasticsearchClientTest, GetBuildsDocumentEndpointWithoutBodyHeaders) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.response.status_code = 200;
  http_client->next_result.response.body = R"({"_id":"42"})";
  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";

  ElasticsearchClient client =
      ElasticsearchClient::Create(http_client, ::std::move(config));

  const ElasticsearchResult result = client.Get("/movies", "/42");

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(http_client->requests.size(), 1);
  const HttpRequest& request = http_client->requests.front();
  EXPECT_EQ(request.method, HttpMethod::kGet);
  EXPECT_EQ(request.url, "http://localhost:9200/movies/_doc/42");
  EXPECT_TRUE(request.body.empty());
  EXPECT_EQ(FindHeader(request.headers, "Accept"), "application/json");
  EXPECT_EQ(FindHeader(request.headers, "Content-Type"), ::std::nullopt);
  EXPECT_EQ(FindHeader(request.headers, "Authorization"), ::std::nullopt);
}

TEST(ElasticsearchClientTest, HealthBuildsClusterHealthEndpoint) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.response.status_code = 200;
  http_client->next_result.response.body = R"({"status":"green"})";
  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200///";

  ElasticsearchClient client =
      ElasticsearchClient::Create(http_client, ::std::move(config));

  const ElasticsearchResult result = client.Health();

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.status_code, 200);
  EXPECT_EQ(result.body, R"({"status":"green"})");

  ASSERT_EQ(http_client->requests.size(), 1);
  const HttpRequest& request = http_client->requests.front();
  EXPECT_EQ(request.method, HttpMethod::kGet);
  EXPECT_EQ(request.url, "http://localhost:9200/_cluster/health");
  EXPECT_TRUE(request.body.empty());
  EXPECT_EQ(FindHeader(request.headers, "Accept"), "application/json");
  EXPECT_EQ(FindHeader(request.headers, "Content-Type"), ::std::nullopt);
}

TEST(ElasticsearchClientTest, PreservesHttpErrorsInResult) {
  auto http_client = ::std::make_shared<FakeHttpClient>();
  http_client->next_result.error_code = HttpErrorCode::kHttpStatusError;
  http_client->next_result.error_message = "HTTP request returned status 404";
  http_client->next_result.response.status_code = 404;
  http_client->next_result.response.body = R"({"found":false})";
  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";

  ElasticsearchClient client =
      ElasticsearchClient::Create(http_client, ::std::move(config));

  const ElasticsearchResult result = client.Get("movies", "missing");

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.status_code, 404);
  EXPECT_EQ(result.body, R"({"found":false})");
  EXPECT_EQ(result.error_message, "HTTP request returned status 404");
}

TEST(ElasticsearchClientTest, RejectsInvalidConstruction) {
  ElasticsearchClient::Config valid_config;
  valid_config.base_url = "http://localhost:9200";
  ElasticsearchClient::Config invalid_config;

  EXPECT_THROW(
      ElasticsearchClient::Create(nullptr, valid_config),
      ::std::invalid_argument);

  EXPECT_THROW(
      ElasticsearchClient::Create(::std::make_shared<FakeHttpClient>(), invalid_config),
      ::std::invalid_argument);
}

TEST(ElasticsearchClientTest, CreateBuildsClientWithDefaultHttpTransport) {
  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";

  EXPECT_NO_THROW({
    ElasticsearchClient::Create(::std::move(config));
  });
}

TEST(ElasticsearchClientTest, CreateAcceptsHttpTransportConfig) {
  ElasticsearchClient::Config config;
  config.base_url = "http://localhost:9200";
  config.http_config.pool_size = 1;
  config.http_config.acquire_timeout = milliseconds(10);

  EXPECT_NO_THROW({
    ElasticsearchClient::Create(::std::move(config));
  });
}

}  // namespace
}  // namespace utilities
}  // namespace shooting_star
