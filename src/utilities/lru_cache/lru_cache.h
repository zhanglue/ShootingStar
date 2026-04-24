#pragma once

#include <chrono>
#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace shooting_star {
namespace utilities {

template <typename Key, typename Value>
class LruCache {
 public:
  using Clock = ::std::chrono::steady_clock;
  using Duration = Clock::duration;

  LruCache(::std::size_t capacity, Duration ttl)
      : capacity_(capacity), ttl_(ttl) {}

  ::std::optional<Value> Get(const Key& key) {
    const ::std::lock_guard<::std::mutex> lock(mutex_);

    const auto iter = index_.find(key);
    if (iter == index_.end()) {
      return ::std::nullopt;
    }

    const Clock::time_point now = Clock::now();
    if (IsExpired(*iter->second, now)) {
      entries_.erase(iter->second);
      index_.erase(iter);
      return ::std::nullopt;
    }

    entries_.splice(entries_.begin(), entries_, iter->second);
    return entries_.front().value;
  }

  void Put(Key key, Value value) {
    const ::std::lock_guard<::std::mutex> lock(mutex_);

    if (capacity_ == 0 || ttl_ <= Duration::zero()) {
      return;
    }

    const Clock::time_point now = Clock::now();
    const auto iter = index_.find(key);
    if (iter != index_.end()) {
      iter->second->value = ::std::move(value);
      iter->second->expires_at = now + ttl_;
      entries_.splice(entries_.begin(), entries_, iter->second);
      return;
    }

    entries_.push_front(Entry{
        ::std::move(key),
        ::std::move(value),
        now + ttl_,
    });
    index_[entries_.front().key] = entries_.begin();

    while (entries_.size() > capacity_) {
      index_.erase(entries_.back().key);
      entries_.pop_back();
    }
  }

  ::std::size_t capacity() const { return capacity_; }
  ::std::size_t size() const {
    const ::std::lock_guard<::std::mutex> lock(mutex_);
    return entries_.size();
  }

 private:
  struct Entry {
    Key key;
    Value value;
    Clock::time_point expires_at;
  };

  bool IsExpired(const Entry& entry, Clock::time_point now) const {
    return now >= entry.expires_at;
  }

  ::std::size_t capacity_;
  Duration ttl_;
  mutable ::std::mutex mutex_;
  ::std::list<Entry> entries_;
  ::std::unordered_map<Key, typename ::std::list<Entry>::iterator> index_;
};

}  // namespace utilities
}  // namespace shooting_star
