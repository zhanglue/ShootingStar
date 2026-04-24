#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace shooting_star {
namespace utilities {

enum class HttpMethod {
  kGet,
  kPost,
  kPut,
  kDelete,
  kPatch,
  kHead,
};

struct HttpHeader {
  ::std::string name;
  ::std::string value;
};

struct HttpRequest {
  HttpMethod method = HttpMethod::kGet;
  ::std::string url;
  ::std::vector<HttpHeader> headers;
  ::std::string body;
  ::std::optional<::std::chrono::milliseconds> timeout;
};

struct HttpResponse {
  int status_code = 0;
  ::std::vector<HttpHeader> headers;
  ::std::string body;
};

enum class HttpErrorCode {
  kNone,
  kPoolAcquireTimeout,
  kTransportError,
  kHttpStatusError,
  kInvalidArgument,
};

struct HttpResult {
  HttpErrorCode error_code = HttpErrorCode::kNone;
  ::std::string error_message;
  HttpResponse response;

  bool ok() const { return error_code == HttpErrorCode::kNone; }
};

class HttpClient {
 public:
  static constexpr int kMinHttpStatusCode = 100;
  static constexpr int kMaxHttpStatusCode = 599;

  virtual ~HttpClient() = default;

  HttpResult Get(::std::string url,
                 ::std::vector<HttpHeader> headers = {},
                 ::std::optional<::std::chrono::milliseconds> timeout = ::std::nullopt) {
    HttpRequest request;
    request.method = HttpMethod::kGet;
    request.url = ::std::move(url);
    request.headers = ::std::move(headers);
    request.timeout = timeout;
    return Execute(request);
  }

  HttpResult Post(::std::string url,
                  ::std::string body,
                  ::std::vector<HttpHeader> headers = {},
                  ::std::optional<::std::chrono::milliseconds> timeout = ::std::nullopt) {
    HttpRequest request;
    request.method = HttpMethod::kPost;
    request.url = ::std::move(url);
    request.headers = ::std::move(headers);
    request.body = ::std::move(body);
    request.timeout = timeout;
    return Execute(request);
  }

 protected:
  virtual HttpResult Execute(const HttpRequest& request) = 0;
};

}  // namespace utilities
}  // namespace shooting_star
