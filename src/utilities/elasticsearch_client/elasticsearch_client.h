/*
 * ElasticsearchClient is the project-level typed wrapper for the subset of the
 * Elasticsearch HTTP API that the recommender services need. The class is
 * responsible for constructing Elasticsearch request paths, normalizing the base
 * URL and request paths, adding JSON and Basic Authentication headers, applying
 * per-request timeout overrides, and translating HttpResult values into
 * ElasticsearchResult values.
 *
 * Callers should create instances through the static Create methods. The
 * single-argument Create method builds a default CurlHttpClient-backed transport
 * using Config::http_config. The overload that accepts a shared HttpClient is
 * intended for tests or advanced integration points that need to provide a
 * custom transport implementation.
 */

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/utilities/http_client/curl_http_client.h"
#include "src/utilities/http_client/http_client.h"

namespace shooting_star {
namespace utilities {

struct ElasticsearchResult {
  ElasticsearchResult();
  explicit ElasticsearchResult(HttpResult&& http_result);

  bool ok = false;
  int status_code = 0;
  ::std::string body;
  ::std::string error_message;
};

class ElasticsearchClient {
 public:
  struct Config {
    Config();

    ::std::string base_url;
    ::std::string username;
    ::std::string password;
    ::std::optional<::std::chrono::milliseconds> request_timeout;
    CurlHttpClient::Config http_config;
  };

  static ElasticsearchClient Create(Config config);
  static ElasticsearchClient Create(::std::shared_ptr<HttpClient> http_client,
                                    Config config);

  ElasticsearchResult Health() const;
  ElasticsearchResult Get(::std::string index, ::std::string id) const;
  ElasticsearchResult Search(::std::string index, ::std::string query_json) const;

 private:
  ElasticsearchClient(::std::shared_ptr<HttpClient> http_client,
                      Config config);

  ::std::string BuildUrl(::std::string path) const;
  ::std::vector<HttpHeader> BuildHeaders(bool has_body) const;

  ::std::shared_ptr<HttpClient> http_client_;
  Config config_;
};

}  // namespace utilities
}  // namespace shooting_star
