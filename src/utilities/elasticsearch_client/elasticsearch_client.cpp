#include "src/utilities/elasticsearch_client/elasticsearch_client.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "src/utilities/global_config/global_config.h"
#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {

using ::std::invalid_argument;
using ::std::make_shared;
using ::std::shared_ptr;
using ::std::string;
using ::std::vector;
using ::std::chrono::milliseconds;

namespace {

constexpr int kDefaultRequestTimeoutMs = 100;
constexpr int kDefaultHttpClientAcquireTimeoutMs = 30;
constexpr int kDefaultHttpClientRequestTimeoutMs = 30;
constexpr int kDefaultHttpClientConnectTimeoutMs = 20;
constexpr int kDefaultHttpClientRetryMaxAttempts = 3;
constexpr int kDefaultHttpClientRetryDelayMs = 0;

void ValidateElasticsearchTimeoutConfig(
    const ElasticsearchClient::Config& config) {
  ValidateTimeoutNotGreater("elasticsearch.http_config.connect_timeout",
                            config.http_config.connect_timeout,
                            "elasticsearch.http_config.request_timeout",
                            config.http_config.request_timeout);
  if (!config.request_timeout.has_value()) {
    return;
  }
  ValidateTimeoutNotGreater("elasticsearch.http_config.acquire_timeout",
                            config.http_config.acquire_timeout,
                            "elasticsearch.request_timeout",
                            *config.request_timeout);
  ValidateTimeoutNotGreater("elasticsearch.http_config.request_timeout",
                            config.http_config.request_timeout,
                            "elasticsearch.request_timeout",
                            *config.request_timeout);
  ValidateTimeoutSumNotGreater("elasticsearch.http_config.acquire_timeout",
                               config.http_config.acquire_timeout,
                               "elasticsearch.http_config.request_timeout",
                               config.http_config.request_timeout,
                               "elasticsearch.request_timeout",
                               *config.request_timeout);
}

}  // namespace

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

ElasticsearchClient::Config::Config()
    : request_timeout(milliseconds(kDefaultRequestTimeoutMs)) {
  http_config.acquire_timeout =
      milliseconds(kDefaultHttpClientAcquireTimeoutMs);
  http_config.request_timeout =
      milliseconds(kDefaultHttpClientRequestTimeoutMs);
  http_config.connect_timeout =
      milliseconds(kDefaultHttpClientConnectTimeoutMs);
  http_config.acquire_retry.max_attempts = kDefaultHttpClientRetryMaxAttempts;
  http_config.acquire_retry.delay = milliseconds(kDefaultHttpClientRetryDelayMs);
  http_config.connect_retry.max_attempts = kDefaultHttpClientRetryMaxAttempts;
  http_config.connect_retry.delay = milliseconds(kDefaultHttpClientRetryDelayMs);
  http_config.request_retry.max_attempts = kDefaultHttpClientRetryMaxAttempts;
  http_config.request_retry.delay = milliseconds(kDefaultHttpClientRetryDelayMs);
}

////////////////////////////////////////////////////////////////////////////////
// ElasticsearchClient Factory Methods
////////////////////////////////////////////////////////////////////////////////

ElasticsearchClient ElasticsearchClient::Create() {
  const GlobalConfig& global_config = GlobalConfig::Get();
  Config config;
  config.base_url = global_config.GetElasticsearchBaseUrl();
  config.username = global_config.GetElasticsearchUsername();
  config.password = GetEnvOrDefault(global_config.GetElasticsearchPasswordEnv(),
                                    global_config.GetElasticsearchPassword());
  config.request_timeout =
      milliseconds(global_config.GetElasticsearchRequestTimeoutMs());
  config.http_config.pool_size = static_cast<::std::size_t>(
      global_config.GetElasticsearchHttpClientPoolSize());
  config.http_config.acquire_timeout =
      milliseconds(global_config.GetElasticsearchHttpClientAcquireTimeoutMs());
  config.http_config.request_timeout =
      milliseconds(global_config.GetElasticsearchHttpClientRequestTimeoutMs());
  config.http_config.connect_timeout =
      milliseconds(global_config.GetElasticsearchHttpClientConnectTimeoutMs());
  config.http_config.acquire_retry.max_attempts =
      global_config.GetElasticsearchHttpClientAcquireRetryMaxAttempts();
  config.http_config.acquire_retry.delay =
      milliseconds(global_config.GetElasticsearchHttpClientAcquireRetryDelayMs());
  config.http_config.connect_retry.max_attempts =
      global_config.GetElasticsearchHttpClientConnectRetryMaxAttempts();
  config.http_config.connect_retry.delay =
      milliseconds(global_config.GetElasticsearchHttpClientConnectRetryDelayMs());
  config.http_config.request_retry.max_attempts =
      global_config.GetElasticsearchHttpClientRequestRetryMaxAttempts();
  config.http_config.request_retry.delay =
      milliseconds(global_config.GetElasticsearchHttpClientRequestRetryDelayMs());
  config.http_config.follow_redirects =
      global_config.GetElasticsearchHttpClientFollowRedirects();
  config.http_config.verify_ssl =
      global_config.GetElasticsearchHttpClientVerifySsl();
  config.http_config.ca_cert_path =
      global_config.GetElasticsearchHttpClientCaCertPath();
  ValidateTimeoutNotGreater(
      global_config.GetElasticsearchHttpClientAcquireTimeoutMsKey(),
      config.http_config.acquire_timeout,
      global_config.GetElasticsearchRequestTimeoutMsKey(),
      *config.request_timeout);
  ValidateTimeoutNotGreater(
      global_config.GetElasticsearchHttpClientRequestTimeoutMsKey(),
      config.http_config.request_timeout,
      global_config.GetElasticsearchRequestTimeoutMsKey(),
      *config.request_timeout);
  ValidateTimeoutNotGreater(
      global_config.GetElasticsearchHttpClientConnectTimeoutMsKey(),
      config.http_config.connect_timeout,
      global_config.GetElasticsearchHttpClientRequestTimeoutMsKey(),
      config.http_config.request_timeout);
  ValidateTimeoutSumNotGreater(
      global_config.GetElasticsearchHttpClientAcquireTimeoutMsKey(),
      config.http_config.acquire_timeout,
      global_config.GetElasticsearchHttpClientRequestTimeoutMsKey(),
      config.http_config.request_timeout,
      global_config.GetElasticsearchRequestTimeoutMsKey(),
      *config.request_timeout);
  return Create(std::move(config));
}

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
  ValidateElasticsearchTimeoutConfig(config_);
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
