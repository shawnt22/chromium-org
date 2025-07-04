// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_SQL_CACHE_ENTRY_KEY_H_
#define NET_DISK_CACHE_SQL_CACHE_ENTRY_KEY_H_

#include <optional>
#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_memory.h"
#include "net/base/net_export.h"

namespace disk_cache {

// Represents the key for a cache entry in the SQL disk cache backend.
//
// This class is a wrapper around the cache key string, which is generated by
// HttpCache::GenerateCacheKeyForRequest(). These keys can be long, and the
// SQL backend uses them as keys in multiple in-memory maps (e.g., for tracking
// active, doomed, and recently used entries). The key is also passed between
// threads for database operations.
//
// To avoid high memory consumption from duplicating these long strings, this
// class holds the key in a `scoped_refptr<base::RefCountedString>`. This
// allows multiple data structures to share the same underlying string data
// cheaply, reducing overall memory usage.
//
// The class provides comparison operators and a `std::hash` specialization so
// it can be used efficiently as a key in both ordered and unordered STL
// containers.
//
// Future Work: For the Renderer-Accessible HTTP Cache project, this class is
// expected to be extended to also hold a cache isolation key, in addition to
// the main cache key string.
class NET_EXPORT_PRIVATE CacheEntryKey {
 public:
  explicit CacheEntryKey(std::string str = "");
  ~CacheEntryKey();

  CacheEntryKey(const CacheEntryKey& other);
  CacheEntryKey(CacheEntryKey&& other);
  CacheEntryKey& operator=(const CacheEntryKey& other);
  CacheEntryKey& operator=(CacheEntryKey&& other);

  bool operator<(const CacheEntryKey& other) const;
  bool operator==(const CacheEntryKey& other) const;

  const std::string& string() const;

 private:
  scoped_refptr<const base::RefCountedString> data_;
};

}  // namespace disk_cache

namespace std {

// Implement hashing of CacheEntryKey, so it can be used as key in STL
// containers.
template <>
struct hash<disk_cache::CacheEntryKey> {
  std::size_t operator()(const disk_cache::CacheEntryKey& k) const {
    return std::hash<std::string>{}(k.string());
  }
};

}  // namespace std

#endif  // NET_DISK_CACHE_SQL_CACHE_ENTRY_KEY_H_
