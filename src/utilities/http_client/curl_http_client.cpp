#include "src/utilities/http_client/curl_http_client.h"

#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {

using ::std::format;
using ::std::invalid_argument;
using ::std::make_shared;
using ::std::optional;
using ::std::shared_ptr;
using ::std::string;
using ::std::string_view;
using ::std::vector;
using ::std::chrono::milliseconds;

namespace {

// Local types and helpers in this namespace adapt our C++ HTTP client model to
// libcurl's C-style callback and option APIs. They keep the raw curl handles,
// header lists, callbacks, and result construction details contained within
// this translation unit.

class CurlHeaderList {
 public:
  CurlHeaderList() = default;
  ~CurlHeaderList() {
    if (headers_ != nullptr) {
      curl_slist_free_all(headers_);
    }
  }

  CurlHeaderList(const CurlHeaderList&) = delete;
  CurlHeaderList& operator=(const CurlHeaderList&) = delete;

  bool Append(const HttpHeader& header) {
    const string serialized = header.name + ": " + header.value;
    curl_slist* updated = curl_slist_append(headers_, serialized.c_str());
    if (updated == nullptr) {
      return false;
    }
    headers_ = updated;
    return true;
  }

  curl_slist* get() const { return headers_; }

 private:
  curl_slist* headers_ = nullptr;
};

size_t WriteBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* body = static_cast<string*>(userdata);
  const size_t bytes = size * nmemb;
  body->append(ptr, bytes);
  return bytes;
}

size_t WriteHeaderCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* headers = static_cast<vector<HttpHeader>*>(userdata);
  const size_t bytes = size * nmemb;
  string_view line(ptr, bytes);
  TrimWhitespace(line);
  if (line.empty() || line.starts_with("HTTP/")) {
    return bytes;
  }

  const size_t separator = line.find(':');
  if (separator == string_view::npos) {
    return bytes;
  }

  string_view name = line.substr(0, separator);
  string_view value = line.substr(separator + 1);
  TrimWhitespace(name);
  TrimWhitespace(value);

  headers->push_back(HttpHeader{
      string(name),
      string(value),
  });
  return bytes;
}

const char* MethodName(HttpMethod method) {
  switch (method) {
    case HttpMethod::kGet:
      return "GET";
    case HttpMethod::kPost:
      return "POST";
    case HttpMethod::kPut:
      return "PUT";
    case HttpMethod::kDelete:
      return "DELETE";
    case HttpMethod::kPatch:
      return "PATCH";
    case HttpMethod::kHead:
      return "HEAD";
  }
  return "GET";
}

bool MethodAllowsBody(HttpMethod method) {
  return method == HttpMethod::kPost || method == HttpMethod::kPut ||
         method == HttpMethod::kPatch || method == HttpMethod::kDelete;
}

bool IsSupportedHttpStatusCode(long status_code) {
  return status_code >= HttpClient::kMinHttpStatusCode &&
         status_code <= HttpClient::kMaxHttpStatusCode;
}

void SetLongOption(CURL* curl, CURLoption option, long value) {
  curl_easy_setopt(curl, option, value);
}

void SetStringOption(CURL* curl, CURLoption option, const char* value) {
  curl_easy_setopt(curl, option, value);
}

void SetPointerOption(CURL* curl, CURLoption option, void* value) {
  curl_easy_setopt(curl, option, value);
}

template <typename Function>
void SetFunctionOption(CURL* curl, CURLoption option, Function function) {
  curl_easy_setopt(curl, option, function);
}

void SetHeaderListOption(CURL* curl, CURLoption option, curl_slist* value) {
  curl_easy_setopt(curl, option, value);
}

void ConfigureMethod(CURL* curl, const HttpRequest& request) {
  switch (request.method) {
    case HttpMethod::kGet:
      SetLongOption(curl, CURLOPT_HTTPGET, 1L);
      break;
    case HttpMethod::kPost:
      SetLongOption(curl, CURLOPT_POST, 1L);
      break;
    case HttpMethod::kHead:
      SetLongOption(curl, CURLOPT_NOBODY, 1L);
      break;
    case HttpMethod::kPut:
    case HttpMethod::kDelete:
    case HttpMethod::kPatch:
      SetStringOption(curl, CURLOPT_CUSTOMREQUEST, MethodName(request.method));
      break;
  }

  if (MethodAllowsBody(request.method)) {
    SetStringOption(curl, CURLOPT_POSTFIELDS, request.body.data());
    SetLongOption(curl, CURLOPT_POSTFIELDSIZE,
                  static_cast<long>(request.body.size()));
  }
}

HttpResult CreateInvalidArgumentResult(string message) {
  HttpResult result;
  result.error_code = HttpErrorCode::kInvalidArgument;
  result.error_message = std::move(message);
  return result;
}

HttpResult CreateUnsupportedHttpStatusResult(long status_code,
                                             HttpResponse response) {
  HttpResult result;
  result.error_code = HttpErrorCode::kTransportError;
  result.error_message =
      format("HTTP response status is outside supported range: {}", status_code);
  result.response = std::move(response);
  return result;
}

CurlHandlePool::Config CreateCurlHandlePoolConfig(
    const CurlHttpClient::Config& config) {
  CurlHandlePool::Config pool_config;
  pool_config.pool_size = config.pool_size;
  pool_config.acquire_timeout = config.acquire_timeout;
  return pool_config;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// CurlHttpClient::Config
////////////////////////////////////////////////////////////////////////////////

CurlHttpClient::Config::Config()
    : pool_size(4),
      acquire_timeout(milliseconds(1000)),
      request_timeout(milliseconds(5000)),
      connect_timeout(milliseconds(1000)),
      follow_redirects(true),
      verify_ssl(true) {}

////////////////////////////////////////////////////////////////////////////////
// CurlHttpClient
////////////////////////////////////////////////////////////////////////////////

CurlHttpClient::CurlHttpClient() : CurlHttpClient(Config{}) {}

CurlHttpClient::CurlHttpClient(Config config)
    : config_(config),
      pool_(make_shared<CurlHandlePool>(CreateCurlHandlePoolConfig(config))) {}

CurlHttpClient::CurlHttpClient(shared_ptr<CurlHandlePool> pool)
    : CurlHttpClient(std::move(pool), Config{}) {}

CurlHttpClient::CurlHttpClient(shared_ptr<CurlHandlePool> pool, Config config)
    : config_(config), pool_(std::move(pool)) {
  if (pool_ == nullptr) {
    throw invalid_argument("CurlHttpClient pool must not be null");
  }
}

// Implements HttpClient's virtual request execution hook on top of libcurl.
// libcurl exposes a C-style function API, so this method does a fair amount of
// alignment work between our C++ request/response types and libcurl's raw
// handles, callbacks, and option setters.
HttpResult CurlHttpClient::Execute(const HttpRequest& request) {
  if (request.url.empty()) {
    return CreateInvalidArgumentResult("HTTP request URL must not be empty");
  }

  optional<CurlHandlePool::Lease> lease = pool_->Acquire();
  if (!lease.has_value()) {
    HttpResult result;
    result.error_code = HttpErrorCode::kPoolAcquireTimeout;
    result.error_message = "Timed out acquiring curl handle from HTTP pool";
    return result;
  }

  CURL* curl = lease->get();
  curl_easy_reset(curl);

  CurlHeaderList header_list;
  for (const HttpHeader& header : request.headers) {
    if (!header_list.Append(header)) {
      return CreateInvalidArgumentResult(
          "Failed to allocate curl request header list");
    }
  }

  HttpResponse response;
  char error_buffer[CURL_ERROR_SIZE] = {};

  SetStringOption(curl, CURLOPT_URL, request.url.c_str());
  SetLongOption(curl, CURLOPT_NOSIGNAL, 1L);
  SetPointerOption(curl, CURLOPT_ERRORBUFFER, error_buffer);
  SetFunctionOption(curl, CURLOPT_WRITEFUNCTION, WriteBodyCallback);
  SetPointerOption(curl, CURLOPT_WRITEDATA, &response.body);
  SetFunctionOption(curl, CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
  SetPointerOption(curl, CURLOPT_HEADERDATA, &response.headers);
  SetLongOption(curl, CURLOPT_FOLLOWLOCATION, config_.follow_redirects ? 1L : 0L);
  SetLongOption(curl, CURLOPT_SSL_VERIFYPEER, config_.verify_ssl ? 1L : 0L);
  SetLongOption(curl, CURLOPT_SSL_VERIFYHOST, config_.verify_ssl ? 2L : 0L);
  SetLongOption(curl, CURLOPT_CONNECTTIMEOUT_MS,
                static_cast<long>(config_.connect_timeout.count()));

  const milliseconds request_timeout =
      request.timeout.value_or(config_.request_timeout);
  SetLongOption(curl, CURLOPT_TIMEOUT_MS,
                static_cast<long>(request_timeout.count()));

  if (header_list.get() != nullptr) {
    SetHeaderListOption(curl, CURLOPT_HTTPHEADER, header_list.get());
  }

  ConfigureMethod(curl, request);

  // It is here to launch the request via libcurl.
  const CURLcode code = curl_easy_perform(curl);

  long curl_response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &curl_response_code);

  HttpResult result;

  if (code != CURLE_OK) {
    result.response = std::move(response);
    result.error_code = HttpErrorCode::kTransportError;
    result.error_message = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);
    return result;
  }

  if (!IsSupportedHttpStatusCode(curl_response_code)) {
    return CreateUnsupportedHttpStatusResult(curl_response_code,
                                             std::move(response));
  }

  response.status_code = static_cast<int>(curl_response_code);
  result.response = std::move(response);

  if (result.response.status_code < 200 || result.response.status_code >= 400) {
    result.error_code = HttpErrorCode::kHttpStatusError;
    result.error_message =
        format("HTTP request returned status {}", result.response.status_code);
    return result;
  }

  return result;
}

}  // namespace utilities
}  // namespace shooting_star
