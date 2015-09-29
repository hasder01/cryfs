#pragma once
#ifndef MESSMER_BLOCKSTORE_IMPLEMENTATIONS_CACHING_CACHE_H_
#define MESSMER_BLOCKSTORE_IMPLEMENTATIONS_CACHING_CACHE_H_

#include "CacheEntry.h"
#include "QueueMap.h"
#include "PeriodicTask.h"
#include <memory>
#include <mutex>
#include <boost/optional.hpp>
#include <future>
#include <messmer/cpp-utils/assert/assert.h>

namespace blockstore {
namespace caching {

template<class Key, class Value>
class Cache {
public:
  static constexpr uint32_t MAX_ENTRIES = 1000;
  //TODO Experiment with good values
  static constexpr double PURGE_LIFETIME_SEC = 0.5; //When an entry has this age, it will be purged from the cache
  static constexpr double PURGE_INTERVAL = 0.5; // With this interval, we check for entries to purge
  static constexpr double MAX_LIFETIME_SEC = PURGE_LIFETIME_SEC + PURGE_INTERVAL; // This is the oldest age an entry can reach (given purging works in an ideal world, i.e. with the ideal interval and in zero time)

  Cache();
  virtual ~Cache();

  void push(const Key &key, Value value);
  boost::optional<Value> pop(const Key &key);

private:
  void _popOldEntriesParallel();
  void _popOldEntries();
  boost::optional<Value> _popOldEntry();
  static void _destructElementsInParallel(std::vector<CacheEntry<Key, Value>> *list);

  mutable std::mutex _mutex;
  QueueMap<Key, CacheEntry<Key, Value>> _cachedBlocks;
  std::unique_ptr<PeriodicTask> _timeoutFlusher;
};

template<class Key, class Value> constexpr uint32_t Cache<Key, Value>::MAX_ENTRIES;
template<class Key, class Value> constexpr double Cache<Key, Value>::PURGE_LIFETIME_SEC;
template<class Key, class Value> constexpr double Cache<Key, Value>::PURGE_INTERVAL;
template<class Key, class Value> constexpr double Cache<Key, Value>::MAX_LIFETIME_SEC;

template<class Key, class Value>
Cache<Key, Value>::Cache(): _cachedBlocks(), _timeoutFlusher(nullptr) {
  //Don't initialize timeoutFlusher in the initializer list,
  //because it then might already call Cache::popOldEntries() before Cache is done constructing.
  _timeoutFlusher = std::make_unique<PeriodicTask>(std::bind(&Cache::_popOldEntriesParallel, this), PURGE_INTERVAL);
}

template<class Key, class Value>
Cache<Key, Value>::~Cache() {
}

template<class Key, class Value>
boost::optional<Value> Cache<Key, Value>::pop(const Key &key) {
  std::lock_guard<std::mutex> lock(_mutex);
  auto found = _cachedBlocks.pop(key);
  if (!found) {
    return boost::none;
  }
  return found->releaseValue();
}

template<class Key, class Value>
void Cache<Key, Value>::push(const Key &key, Value value) {
  std::lock_guard<std::mutex> lock(_mutex);
  ASSERT(_cachedBlocks.size() <= MAX_ENTRIES, "Cache too full");
  if (_cachedBlocks.size() == MAX_ENTRIES) {
    _cachedBlocks.pop();
    ASSERT(_cachedBlocks.size() == MAX_ENTRIES-1, "Removing entry from cache didn't work");
  }
  _cachedBlocks.push(key, CacheEntry<Key, Value>(std::move(value)));
}

template<class Key, class Value>
void Cache<Key, Value>::_popOldEntriesParallel() {
  unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::future<void>> waitHandles;
  for (unsigned int i = 0; i < numThreads; ++i) {
    waitHandles.push_back(std::async(std::launch::async, [this] {
      _popOldEntries();
    }));
  }
  for (auto & waitHandle : waitHandles) {
    waitHandle.wait();
  }
};

template<class Key, class Value>
void Cache<Key, Value>::_popOldEntries() {
  // This function can be called in parallel by multiple threads and will then cause the Value destructors
  // to be called in parallel. The call to _popOldEntry() is synchronized to avoid race conditions,
  // but the Value destructor is called in this function which is not synchronized.
  int num = 0;
  boost::optional<Value> oldEntry = _popOldEntry();
  while (oldEntry != boost::none) {
    ++num;
    oldEntry = _popOldEntry();
  }
}

template<class Key, class Value>
boost::optional<Value> Cache<Key, Value>::_popOldEntry() {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_cachedBlocks.size() > 0 && _cachedBlocks.peek()->ageSeconds() > PURGE_LIFETIME_SEC) {
    return _cachedBlocks.pop()->releaseValue();
  } else {
    return boost::none;
  }
};

}
}

#endif
