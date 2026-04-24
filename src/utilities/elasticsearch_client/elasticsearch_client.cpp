#include "src/utilities/elasticsearch_client/elasticsearch_client.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {

using ::std::invalid_argument;
using ::std::make_shared;
using ::std::shared_ptr;
using ::std::string;
using ::std::vector;

////////////////////////////////////////////////////////////////////////////////
// ElasticsearchResult
////////////////////////////////////////////////////////////////////////////////

ElasticsearchResult::ElasticsearchResult() = default;

ElasticsearchResult::ElasticsearchResult(HttpResult&& http_result)
    : ok(http_result.ok()),
      status_code(http_result.response.status_code),
      body(std::move(http_result.response.body)),
      error_message(std::move(http_result.error_message)) {}

////////////////////////////////////////////////////////////////////////////////
// ElasticsearchClient::Config
////////////////////////////////////////////////////////////////////////////////

ElasticsearchClient::Config::Config() = default;

////////////////////////////////////////////////////////////////////////////////
// ElasticsearchClient Factory Methods
////////////////////////////////////////////////////////////////////////////////

ElasticsearchClient ElasticsearchClient::Create(Config config) {
  CurlHttpClient::Config http_config = config.http_config;
  return ElasticsearchClient(make_shared<CurlHttpClient>(std::move(http_config)),
                             std::move(config));
}

ElasticsearchClient ElasticsearchClient::Create(shared_ptr<HttpClient> http_client,
                                                Config config) {
  return ElasticsearchClient(std::move(http_client), std::move(config));
}

////////////////////////////////////////////////////////////////////////////////
// ElasticsearchClient
////////////////////////////////////////////////////////////////////////////////

ElasticsearchClient::ElasticsearchClient(
    shared_ptr<HttpClient> http_client,
    Config config)
    : http_client_(std::move(http_client)), config_(std::move(config)) {
  if (http_client_ == nullptr) {
    throw invalid_argument("ElasticsearchClient http_client must not be null");
  }
  TrimTrailingSlashes(config_.base_url);
  if (config_.base_url.empty()) {
    throw invalid_argument("ElasticsearchClient base_url must not be empty");
  }
}

ElasticsearchResult ElasticsearchClient::Health() const {
  return ElasticsearchResult(
      http_client_->Get(BuildUrl("/_cluster/health"),
                        BuildHeaders(/*has_body=*/false),
                        config_.request_timeout));
}

ElasticsearchResult ElasticsearchClient::Get(string index, string id) const {
  TrimLeadingSlashes(index);
  TrimLeadingSlashes(id);
  return ElasticsearchResult(
      http_client_->Get(BuildUrl(index + "/_doc/" + id),
                        BuildHeaders(/*has_body=*/false),
                        config_.request_timeout));
}

ElasticsearchResult ElasticsearchClient::Search(string index, string query_json) const {
  TrimLeadingSlashes(index);
  return ElasticsearchResult(
      http_client_->Post(BuildUrl(index + "/_search"),
                         std::move(query_json),
                         BuildHeaders(/*has_body=*/true),
                         config_.request_timeout));
}

string ElasticsearchClient::BuildUrl(string path) const {
  TrimLeadingSlashes(path);
  return config_.base_url + "/" + path;
}

vector<HttpHeader> ElasticsearchClient::BuildHeaders(bool has_body) const {
  vector<HttpHeader> headers;
  headers.push_back(HttpHeader{"Accept", "application/json"});
  if (has_body) {
    headers.push_back(HttpHeader{"Content-Type", "application/json"});
  }

  if (!config_.username.empty() || !config_.password.empty()) {
    headers.push_back(HttpHeader{
        "Authorization",
        "Basic " + Base64Encode(config_.username + ":" + config_.password),
    });
  }

  return headers;
}

}  // namespace utilities
}  // namespace shooting_star
