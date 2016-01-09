#pragma once

#include <list>
#include <set>
#include <memory>
#include <string>
#include <stdexcept>
#include <thread>
#include <sstream>

#include "include/rocksdb/comparator.h"
#include "include/rocksdb/cache_tier.h"
#include "include/rocksdb/cache.h"

#include "cache/blockcache_file.h"
#include "cache/blockcache_metadata.h"
#include "cache/blockcache_file_writer.h"
#include "cache/cache_util.h"
#include "port/port_posix.h"
#include "db/skiplist.h"
#include "util/arena.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include "util/mutexlock.h"
#include "util/histogram.h"

namespace rocksdb {

/**
 * Block cache implementation
 */
class BlockCacheImpl : public CacheTier {
 public:
  BlockCacheImpl(const BlockCacheOptions& opt)
    : opt_(opt),
      insert_ops_(opt_.max_write_pipeline_backlog_size),
      insert_th_(&BlockCacheImpl::InsertMain, this),
      writer_(this, opt_.writer_qdepth) {
    Info(opt_.log, "Initializing allocator. size=%d B count=%d limit=%d B",
         opt_.write_buffer_size, opt_.write_buffer_count,
         opt_.bufferpool_limit);

    bufferAllocator_.Init(opt.write_buffer_size, opt.write_buffer_count,
                          opt.bufferpool_limit);
  }

  virtual ~BlockCacheImpl() {}

  //
  // Override from PageCache
  //
  Status Insert(const Slice& key, const void* data, const size_t size) override;
  Status Lookup(const Slice & key, std::unique_ptr<char[]>* data,
                size_t* size) override;

  //
  // Override from CacheTier
  //
  Status Open() override;
  Status Close() override;
  bool Erase(const Slice& key) override;
  bool Reserve(const size_t size) override;

  std::string PrintStats() override {
    std::ostringstream os;
    os << "Blockcache stats: " << std::endl
       << "* bytes piplined: " << std::endl
       << stats_.bytes_pipelined_.ToString() << std::endl
       << "* bytes written:" << std::endl
       << stats_.bytes_written_.ToString() << std::endl
       << "* bytes read:" << std::endl
       << stats_.bytes_read_.ToString() << std::endl
       << "* cache_hits:" << std::endl
       << stats_.cache_hits_ << std::endl
       << "* cache_misses:" << std::endl
       << stats_.cache_misses_ << std::endl;
    return os.str();
  }

  void Flush_TEST() override {
    while (insert_ops_.Size()) {
      sleep(1);
    }
  }

 private:
  //
  // Insert operation abstraction
  //
  struct InsertOp {
    explicit InsertOp(const bool exit_loop)
      : exit_loop_(exit_loop) {}
    explicit InsertOp(std::string&& key, std::unique_ptr<char[]>&& data,
                      const size_t size)
      : key_(std::move(key)), data_(std::move(data)), size_(size) {}
    ~InsertOp() {}

    InsertOp() = delete;
    InsertOp(InsertOp&) = delete;
    InsertOp(InsertOp&& rhs) = default;
    InsertOp& operator=(InsertOp&& rhs) = default;

    size_t Size() const { return size_; }

    std::string key_;
    std::unique_ptr<char[]> data_;
    const size_t size_ = 0;
    const bool exit_loop_ = false;
  };

  // entry point for insert thread
  void InsertMain();
  // insert implementation
  Status InsertImpl(const Slice& key, const std::unique_ptr<char[]>& buf,
                    const size_t size);
  // Create a new cache file
  void NewCacheFile();
  // Get cache directory path
  std::string GetCachePath() const { return opt_.path + "/cache"; }

  //
  // Statistics
  //
  struct Stats {
    HistogramImpl bytes_pipelined_;
    HistogramImpl bytes_written_;
    HistogramImpl bytes_read_;
    uint64_t cache_hits_ = 0;
    uint64_t cache_misses_ = 0;
  };

  port::RWMutex lock_;                  // Synchronization
  const BlockCacheOptions opt_;         // BlockCache options
  BoundedQueue<InsertOp> insert_ops_;   // Ops waiting for insert
  std::thread insert_th_;               // Insert thread
  uint32_t writerCacheId_ = 0;          // Current cache file identifier
  WriteableCacheFile* cacheFile_ = nullptr;   // Current cache file reference
  CacheWriteBufferAllocator bufferAllocator_; // Buffer provider
  ThreadedWriter writer_;               // Writer threads
  BlockCacheMetadata metadata_;         // Cache meta data manager
  std::atomic<uint64_t> size_{0};       // Size of the cache
  Stats stats_;                         // Statistics
};

}  // namespace rocksdb
