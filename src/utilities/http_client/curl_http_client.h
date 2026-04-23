#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "src/utilities/http_client/curl_handle_pool.h"
#include "src/utilities/http_client/http_client.h"

namespace shooting_star {
namespace utilities {

class CurlHttpClient : public HttpClient {
 public:
  struct Config {
    Config();

    ::std::size_t pool_size;
    ::std::chrono::milliseconds acquire_timeout;
    ::std::chrono::milliseconds request_timeout;
    ::std::chrono::milliseconds connect_timeout;
    bool follow_redirects;
    bool verify_ssl;
    ::std::string ca_cert_path;
    ::std::vector<::std::string> resolve_hosts;
  };

  CurlHttpClient();
  explicit CurlHttpClient(Config config);
  explicit CurlHttpClient(::std::shared_ptr<CurlHandlePool> pool);
  CurlHttpClient(::std::shared_ptr<CurlHandlePool> pool, Config config);

 private:
  HttpResult Execute(const HttpRequest& request) override;

  Config config_;
  ::std::shared_ptr<CurlHandlePool> pool_;
};

}  // namespace utilities
}  // namespace shooting_star
