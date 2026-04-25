#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>

#include <curl/curl.h>

#include "src/utilities/resource_pool/resource_pool.h"

namespace shooting_star {
namespace utilities {

class CurlGlobalInitializer {
 public:
  CurlGlobalInitializer();
  ~CurlGlobalInitializer();

  CurlGlobalInitializer(const CurlGlobalInitializer&) = delete;
  CurlGlobalInitializer& operator=(const CurlGlobalInitializer&) = delete;
};

class CurlHandle {
 public:
  CurlHandle();
  ~CurlHandle();

  CurlHandle(const CurlHandle&) = delete;
  CurlHandle& operator=(const CurlHandle&) = delete;

  CURL* get() const { return curl_; }

 private:
  CURL* curl_ = nullptr;
};

class CurlHandlePool {
 public:
  struct Config {
    Config();

    ::std::size_t pool_size;
    ::std::chrono::milliseconds acquire_timeout;
  };

  class Lease {
   public:
    Lease() = default;
    explicit Lease(ResourcePool<CurlHandle>::Lease lease);
    ~Lease();

    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    Lease(Lease&& other) noexcept;
    Lease& operator=(Lease&& other) noexcept;

    CURL* get() const;
    explicit operator bool() const { return static_cast<bool>(lease_); }

   private:
    ResourcePool<CurlHandle>::Lease lease_;
  };

  CurlHandlePool();
  explicit CurlHandlePool(Config config);

  CurlHandlePool(const CurlHandlePool&) = delete;
  CurlHandlePool& operator=(const CurlHandlePool&) = delete;

  ::std::optional<Lease> Acquire();
  ::std::optional<Lease> Acquire(::std::chrono::milliseconds timeout);
  ::std::size_t size() const;
  ::std::size_t available() const;

 private:
  ::std::shared_ptr<CurlGlobalInitializer> global_initializer_;
  ResourcePool<CurlHandle> pool_;
};

}  // namespace utilities
}  // namespace shooting_star
