#include "src/utilities/http_client/curl_http_client.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "src/utilities/runtime_utilities/runtime_utilities.h"

namespace shooting_star {
namespace utilities {

using ::std::format;
using ::std::filesystem::path;
using ::std::invalid_argument;
using ::std::make_shared;
using ::std::optional;
using ::std::shared_ptr;
using ::std::string;
using ::std::string_view;
using ::std::vector;
using ::std::chrono::milliseconds;
using ::std::chrono::steady_clock;

namespace {

// Local types and helpers in this namespace adapt our C++ HTTP client model to
// libcurl's C-style callback and option APIs. They keep the raw curl handles,
// header lists, callbacks, and result construction details contained within
// this translation unit.

enum class RetryStage {
  kNone,
  kConnect,
  kRequest,
};

struct CurlAttemptResult {
  HttpResult result;
  RetryStage retry_stage = RetryStage::kNone;
};

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

class CurlStringList {
 public:
  CurlStringList() = default;
  ~CurlStringList() {
    if (values_ != nullptr) {
      curl_slist_free_all(values_);
    }
  }

  CurlStringList(const CurlStringList&) = delete;
  CurlStringList& operator=(const CurlStringList&) = delete;

  bool Append(const string& value) {
    curl_slist* updated = curl_slist_append(values_, value.c_str());
    if (updated == nullptr) {
      return false;
    }
    values_ = updated;
    return true;
  }

  curl_slist* get() const { return values_; }

 private:
  curl_slist* values_ = nullptr;
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

bool IsRetryableConnectError(CURLcode code) {
  return code == CURLE_COULDNT_RESOLVE_PROXY ||
         code == CURLE_COULDNT_RESOLVE_HOST ||
         code == CURLE_COULDNT_CONNECT ||
         code == CURLE_SSL_CONNECT_ERROR;
}

bool IsRetryableRequestError(CURLcode code) {
  return code == CURLE_SEND_ERROR ||
         code == CURLE_RECV_ERROR ||
         code == CURLE_GOT_NOTHING ||
         code == CURLE_PARTIAL_FILE ||
         code == CURLE_OPERATION_TIMEDOUT;
}

bool IsRetryableHttpStatus(int status_code) {
  return status_code == 429 || status_code >= 500;
}

HttpResult CreateDeadlineExceededResult() {
  HttpResult result;
  result.error_code = HttpErrorCode::kTransportError;
  result.error_message = "HTTP request timed out before retry attempt";
  return result;
}

optional<milliseconds> RemainingTimeout(
    optional<steady_clock::time_point> deadline) {
  if (!deadline.has_value()) {
    return ::std::nullopt;
  }

  const auto remaining = *deadline - steady_clock::now();
  if (remaining <= steady_clock::duration::zero()) {
    return milliseconds(0);
  }
  const milliseconds remaining_ms =
      ::std::chrono::duration_cast<milliseconds>(remaining);
  if (remaining_ms.count() <= 0) {
    return milliseconds(0);
  }
  return remaining_ms;
}

bool HasTimeRemaining(optional<steady_clock::time_point> deadline) {
  const optional<milliseconds> remaining = RemainingTimeout(deadline);
  return !remaining.has_value() || remaining->count() > 0;
}

milliseconds MinTimeout(milliseconds first, milliseconds second) {
  return first <= second ? first : second;
}

milliseconds BoundByDeadline(
    milliseconds timeout,
    optional<steady_clock::time_point> deadline) {
  const optional<milliseconds> remaining = RemainingTimeout(deadline);
  if (!remaining.has_value()) {
    return timeout;
  }
  return MinTimeout(timeout, *remaining);
}

void SleepBeforeRetry(milliseconds delay,
                      optional<steady_clock::time_point> deadline) {
  const milliseconds bounded_delay = BoundByDeadline(delay, deadline);
  if (bounded_delay.count() > 0) {
    ::std::this_thread::sleep_for(bounded_delay);
  }
}

void ValidateRetryConfig(string_view name, const RetryConfig& retry_config) {
  if (retry_config.max_attempts <= 0) {
    throw invalid_argument(string(name) + ".max_attempts must be greater than 0");
  }
  if (retry_config.delay.count() < 0) {
    throw invalid_argument(string(name) + ".delay must not be negative");
  }
}

void ValidateCurlHttpClientConfig(const CurlHttpClient::Config& config) {
  ValidateRetryConfig("CurlHttpClient acquire_retry", config.acquire_retry);
  ValidateRetryConfig("CurlHttpClient connect_retry", config.connect_retry);
  ValidateRetryConfig("CurlHttpClient request_retry", config.request_retry);
}

CurlHandlePool::Config CreateCurlHandlePoolConfig(
    const CurlHttpClient::Config& config) {
  CurlHandlePool::Config pool_config;
  pool_config.pool_size = config.pool_size;
  pool_config.acquire_timeout = config.acquire_timeout;
  return pool_config;
}

CurlAttemptResult PerformOnceWithLease(
    const CurlHttpClient::Config& config,
    const HttpRequest& request,
    CurlHandlePool::Lease lease,
    milliseconds request_timeout) {
  CURL* curl = lease.get();
  curl_easy_reset(curl);

  CurlHeaderList header_list;
  for (const HttpHeader& header : request.headers) {
    if (!header_list.Append(header)) {
      return CurlAttemptResult{
          CreateInvalidArgumentResult(
              "Failed to allocate curl request header list"),
          RetryStage::kNone,
      };
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
  SetLongOption(curl, CURLOPT_FOLLOWLOCATION,
                config.follow_redirects ? 1L : 0L);
  SetLongOption(curl, CURLOPT_SSL_VERIFYPEER, config.verify_ssl ? 1L : 0L);
  SetLongOption(curl, CURLOPT_SSL_VERIFYHOST, config.verify_ssl ? 2L : 0L);
  if (!config.ca_cert_path.empty()) {
    SetStringOption(curl, CURLOPT_CAINFO, config.ca_cert_path.c_str());
    const string ca_cert_dir = path(config.ca_cert_path).parent_path().string();
    if (!ca_cert_dir.empty()) {
      SetStringOption(curl, CURLOPT_CAPATH, ca_cert_dir.c_str());
    }
  }
  const milliseconds connect_timeout =
      MinTimeout(config.connect_timeout, request_timeout);
  SetLongOption(curl, CURLOPT_CONNECTTIMEOUT_MS,
                static_cast<long>(connect_timeout.count()));
  SetLongOption(curl, CURLOPT_TIMEOUT_MS,
                static_cast<long>(request_timeout.count()));

  if (header_list.get() != nullptr) {
    SetHeaderListOption(curl, CURLOPT_HTTPHEADER, header_list.get());
  }

  CurlStringList resolve_list;
  for (const string& resolve_host : config.resolve_hosts) {
    if (!resolve_list.Append(resolve_host)) {
      return CurlAttemptResult{
          CreateInvalidArgumentResult(
              "Failed to allocate curl host resolve list"),
          RetryStage::kNone,
      };
    }
  }
  if (resolve_list.get() != nullptr) {
    SetHeaderListOption(curl, CURLOPT_RESOLVE, resolve_list.get());
  }

  ConfigureMethod(curl, request);

  // libcurl easy handles one transfer at a time and does not provide a general
  // whole-request retry option, so retry policy is applied by the caller.
  const CURLcode code = curl_easy_perform(curl);

  long curl_response_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &curl_response_code);

  HttpResult result;

  if (code != CURLE_OK) {
    result.response = std::move(response);
    result.error_code = HttpErrorCode::kTransportError;
    result.error_message =
        error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);

    RetryStage retry_stage = RetryStage::kNone;
    if (code == CURLE_OPERATION_TIMEDOUT && curl_response_code == 0) {
      retry_stage = RetryStage::kConnect;
    } else if (IsRetryableConnectError(code)) {
      retry_stage = RetryStage::kConnect;
    } else if (IsRetryableRequestError(code)) {
      retry_stage = RetryStage::kRequest;
    }
    return CurlAttemptResult{std::move(result), retry_stage};
  }

  if (!IsSupportedHttpStatusCode(curl_response_code)) {
    return CurlAttemptResult{
        CreateUnsupportedHttpStatusResult(curl_response_code,
                                          std::move(response)),
        RetryStage::kNone,
    };
  }

  response.status_code = static_cast<int>(curl_response_code);
  result.response = std::move(response);

  if (result.response.status_code < 200 || result.response.status_code >= 400) {
    result.error_code = HttpErrorCode::kHttpStatusError;
    result.error_message =
        format("HTTP request returned status {}", result.response.status_code);
    const RetryStage retry_stage =
        IsRetryableHttpStatus(result.response.status_code)
            ? RetryStage::kRequest
            : RetryStage::kNone;
    return CurlAttemptResult{
        std::move(result),
        retry_stage,
    };
  }

  return CurlAttemptResult{std::move(result), RetryStage::kNone};
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// RetryConfig
////////////////////////////////////////////////////////////////////////////////

RetryConfig::RetryConfig()
    : max_attempts(1),
      delay(milliseconds(0)) {}

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
      pool_(make_shared<CurlHandlePool>(CreateCurlHandlePoolConfig(config))) {
  ValidateCurlHttpClientConfig(config_);
}

CurlHttpClient::CurlHttpClient(shared_ptr<CurlHandlePool> pool)
    : CurlHttpClient(std::move(pool), Config{}) {}

CurlHttpClient::CurlHttpClient(shared_ptr<CurlHandlePool> pool, Config config)
    : config_(config), pool_(std::move(pool)) {
  if (pool_ == nullptr) {
    throw invalid_argument("CurlHttpClient pool must not be null");
  }
  ValidateCurlHttpClientConfig(config_);
}

// Implements HttpClient's virtual request execution hook on top of libcurl.
// libcurl exposes a C-style function API, so this method does a fair amount of
// alignment work between our C++ request/response types and libcurl's raw
// handles, callbacks, and option setters.
HttpResult CurlHttpClient::Execute(const HttpRequest& request) {
  if (request.url.empty()) {
    return CreateInvalidArgumentResult("HTTP request URL must not be empty");
  }

  HttpResult last_result;
  int connect_attempts = 0;
  int request_attempts = 0;
  const optional<steady_clock::time_point> deadline =
      request.timeout.has_value()
          ? optional<steady_clock::time_point>(
                steady_clock::now() + *request.timeout)
          : ::std::nullopt;

  while (true) {
    if (!HasTimeRemaining(deadline)) {
      return CreateDeadlineExceededResult();
    }

    optional<CurlHandlePool::Lease> lease;
    for (int acquire_attempt = 1;
         acquire_attempt <= config_.acquire_retry.max_attempts;
         ++acquire_attempt) {
      const milliseconds acquire_timeout =
          BoundByDeadline(config_.acquire_timeout, deadline);
      if (acquire_timeout.count() <= 0) {
        return CreateDeadlineExceededResult();
      }
      lease = pool_->Acquire(acquire_timeout);
      if (lease.has_value()) {
        break;
      }
      if (acquire_attempt < config_.acquire_retry.max_attempts) {
        SleepBeforeRetry(config_.acquire_retry.delay, deadline);
      }
    }
    if (!lease.has_value()) {
      HttpResult result;
      result.error_code = HttpErrorCode::kPoolAcquireTimeout;
      result.error_message =
          format("Timed out acquiring curl handle from HTTP pool after {} "
                 "attempt(s)",
                 config_.acquire_retry.max_attempts);
      return result;
    }

    const milliseconds request_timeout =
        BoundByDeadline(config_.request_timeout, deadline);
    if (request_timeout.count() <= 0) {
      return CreateDeadlineExceededResult();
    }

    CurlAttemptResult attempt =
        PerformOnceWithLease(config_, request, std::move(*lease),
                             request_timeout);
    last_result = std::move(attempt.result);
    if (last_result.ok() || attempt.retry_stage == RetryStage::kNone) {
      return last_result;
    }

    if (attempt.retry_stage == RetryStage::kConnect) {
      ++connect_attempts;
      if (connect_attempts >= config_.connect_retry.max_attempts) {
        return last_result;
      }
      SleepBeforeRetry(config_.connect_retry.delay, deadline);
      continue;
    }

    ++request_attempts;
    if (request_attempts >= config_.request_retry.max_attempts) {
      return last_result;
    }
    SleepBeforeRetry(config_.request_retry.delay, deadline);
  }
}

}  // namespace utilities
}  // namespace shooting_star
