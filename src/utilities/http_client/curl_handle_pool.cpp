#include "src/utilities/http_client/curl_handle_pool.h"

#include <stdexcept>

namespace shooting_star {
namespace utilities {

namespace {

////////////////////////////////////////////////////////////////////////////////
// CurlGlobalInitializer
////////////////////////////////////////////////////////////////////////////////

::std::shared_ptr<CurlGlobalInitializer> GetCurlGlobalInitializer() {
  static ::std::shared_ptr<CurlGlobalInitializer> initializer =
      ::std::make_shared<CurlGlobalInitializer>();
  return initializer;
}

}  // namespace

CurlGlobalInitializer::CurlGlobalInitializer() {
  const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK) {
    throw ::std::runtime_error("curl_global_init failed");
  }
}

CurlGlobalInitializer::~CurlGlobalInitializer() {
  curl_global_cleanup();
}

////////////////////////////////////////////////////////////////////////////////
// CurlHandle
////////////////////////////////////////////////////////////////////////////////

CurlHandle::CurlHandle() : curl_(curl_easy_init()) {
  if (curl_ == nullptr) {
    throw ::std::runtime_error("curl_easy_init failed");
  }
}

CurlHandle::~CurlHandle() {
  if (curl_ != nullptr) {
    curl_easy_cleanup(curl_);
  }
}

////////////////////////////////////////////////////////////////////////////////
ResourcePool<CurlHandle>::Config ToResourcePoolConfig(
    const CurlHandlePool::Config& config) {
  ResourcePool<CurlHandle>::Config pool_config;
  pool_config.pool_size = config.pool_size;
  pool_config.acquire_timeout = config.acquire_timeout;
  return pool_config;
}

////////////////////////////////////////////////////////////////////////////////
// CurlHandlePool::Lease
////////////////////////////////////////////////////////////////////////////////

CurlHandlePool::Lease::Lease(ResourcePool<CurlHandle>::Lease lease)
    : lease_(::std::move(lease)) {}

CurlHandlePool::Lease::~Lease() {
}

CurlHandlePool::Lease::Lease(Lease&& other) noexcept = default;

CurlHandlePool::Lease& CurlHandlePool::Lease::operator=(Lease&& other) noexcept = default;

CURL* CurlHandlePool::Lease::get() const {
  return lease_ ? lease_->get() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// CurlHandlePool::Config
////////////////////////////////////////////////////////////////////////////////

CurlHandlePool::Config::Config()
    : pool_size(4),
      acquire_timeout(::std::chrono::milliseconds(1000))
      {}

////////////////////////////////////////////////////////////////////////////////
// CurlHandlePool
////////////////////////////////////////////////////////////////////////////////

CurlHandlePool::CurlHandlePool() : CurlHandlePool(Config{}) {}

CurlHandlePool::CurlHandlePool(Config config)
    : global_initializer_(GetCurlGlobalInitializer()),
      pool_(ToResourcePoolConfig(config),
            [] { return ::std::make_unique<CurlHandle>(); }) {}

::std::optional<CurlHandlePool::Lease> CurlHandlePool::Acquire() {
  ::std::optional<ResourcePool<CurlHandle>::Lease> lease = pool_.Acquire();
  if (!lease.has_value()) {
    return ::std::nullopt;
  }
  return Lease(::std::move(*lease));
}

::std::size_t CurlHandlePool::size() const {
  return pool_.size();
}

::std::size_t CurlHandlePool::available() const {
  return pool_.available();
}

}  // namespace utilities
}  // namespace shooting_star
