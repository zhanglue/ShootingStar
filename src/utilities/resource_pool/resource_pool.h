#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace shooting_star {
namespace utilities {

template <typename T>
class ResourcePool {
 public:
  struct Config {
    Config();

    ::std::size_t pool_size;
    ::std::chrono::milliseconds acquire_timeout;
  };

  using Factory = ::std::function<::std::unique_ptr<T>()>;

  class Lease {
   public:
    Lease() = default;
    Lease(ResourcePool* pool, T* resource);
    ~Lease();

    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    Lease(Lease&& other) noexcept;
    Lease& operator=(Lease&& other) noexcept;

    T* get() const;
    T& operator*() const;
    T* operator->() const;
    explicit operator bool() const { return resource_ != nullptr; }

   private:
    void Release();

    ResourcePool* pool_ = nullptr;
    T* resource_ = nullptr;
  };

  explicit ResourcePool(Factory factory);
  ResourcePool(Config config, Factory factory);

  ResourcePool(const ResourcePool&) = delete;
  ResourcePool& operator=(const ResourcePool&) = delete;

  ::std::optional<Lease> Acquire();
  ::std::size_t size() const;
  ::std::size_t available() const;

 private:
  void Release(T* resource);

  Config config_;
  ::std::vector<::std::unique_ptr<T>> resources_;
  ::std::vector<T*> available_resources_;
  mutable ::std::mutex mutex_;
  ::std::condition_variable condition_;
};

template <typename T>
ResourcePool<T>::Config::Config()
    : pool_size(4),
      acquire_timeout(::std::chrono::milliseconds(1000)) {}

template <typename T>
ResourcePool<T>::Lease::Lease(ResourcePool* pool, T* resource)
    : pool_(pool), resource_(resource) {}

template <typename T>
ResourcePool<T>::Lease::~Lease() {
  Release();
}

template <typename T>
ResourcePool<T>::Lease::Lease(Lease&& other) noexcept
    : pool_(other.pool_), resource_(other.resource_) {
  other.pool_ = nullptr;
  other.resource_ = nullptr;
}

template <typename T>
typename ResourcePool<T>::Lease& ResourcePool<T>::Lease::operator=(
    Lease&& other) noexcept {
  if (this != &other) {
    Release();
    pool_ = other.pool_;
    resource_ = other.resource_;
    other.pool_ = nullptr;
    other.resource_ = nullptr;
  }
  return *this;
}

template <typename T>
T* ResourcePool<T>::Lease::get() const {
  return resource_;
}

template <typename T>
T& ResourcePool<T>::Lease::operator*() const {
  return *resource_;
}

template <typename T>
T* ResourcePool<T>::Lease::operator->() const {
  return resource_;
}

template <typename T>
void ResourcePool<T>::Lease::Release() {
  if (pool_ != nullptr && resource_ != nullptr) {
    pool_->Release(resource_);
    pool_ = nullptr;
    resource_ = nullptr;
  }
}

template <typename T>
ResourcePool<T>::ResourcePool(Factory factory)
    : ResourcePool(Config{}, ::std::move(factory)) {}

template <typename T>
ResourcePool<T>::ResourcePool(Config config, Factory factory) : config_(config) {
  if (config_.pool_size == 0) {
    throw ::std::invalid_argument("ResourcePool pool_size must be greater than zero");
  }
  if (!factory) {
    throw ::std::invalid_argument("ResourcePool factory must not be empty");
  }

  resources_.reserve(config_.pool_size);
  available_resources_.reserve(config_.pool_size);
  for (::std::size_t i = 0; i < config_.pool_size; ++i) {
    ::std::unique_ptr<T> resource = factory();
    if (resource == nullptr) {
      throw ::std::runtime_error("ResourcePool factory returned null resource");
    }
    available_resources_.push_back(resource.get());
    resources_.push_back(::std::move(resource));
  }
}

template <typename T>
::std::optional<typename ResourcePool<T>::Lease> ResourcePool<T>::Acquire() {
  ::std::unique_lock lock(mutex_);
  const bool acquired = condition_.wait_for(
      lock, config_.acquire_timeout,
      [this] { return !available_resources_.empty(); });

  if (!acquired) {
    return ::std::nullopt;
  }

  T* resource = available_resources_.back();
  available_resources_.pop_back();
  return Lease(this, resource);
}

template <typename T>
::std::size_t ResourcePool<T>::size() const {
  return resources_.size();
}

template <typename T>
::std::size_t ResourcePool<T>::available() const {
  ::std::lock_guard lock(mutex_);
  return available_resources_.size();
}

template <typename T>
void ResourcePool<T>::Release(T* resource) {
  {
    ::std::lock_guard lock(mutex_);
    if (::std::find(available_resources_.begin(), available_resources_.end(), resource) ==
        available_resources_.end()) {
      available_resources_.push_back(resource);
    }
  }
  condition_.notify_one();
}

}  // namespace utilities
}  // namespace shooting_star
