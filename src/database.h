#ifndef NET_DATABASE_H
#define NET_DATABASE_H

#include <memory>
#include <stdexcept>

#include "common.h"

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

namespace net {

class WriteBatch {
 public:
  template<class K, class V>
  void Write(const K& key, const V& value) {
    stream_key_.Put(key);
    stream_val_.Put(value);
    leveldb::Slice key_slice(reinterpret_cast<const char*>(stream_key_.GetData().data()),
                             stream_key_.GetData().size());
    leveldb::Slice val_slice(reinterpret_cast<const char*>(stream_val_.GetData().data()),
                             stream_val_.GetData().size());
    batch_.Put(key_slice, val_slice);
    ClearStreams();
  }

  template<class V>
  void Write(const uint8_t* key, size_t key_size, const V& value) {
    stream_val_.Put(value);

    leveldb::Slice key_slice(reinterpret_cast<const char*>(key), key_size);
    leveldb::Slice val_slice(reinterpret_cast<const char*>(stream_val_.GetData().data()),
                             stream_val_.GetData().size());
    batch_.Put(key_slice, val_slice);
    ClearStreams();
  }

  template<class K>
  void Remove(const K& key) {
    stream_key_.Put(key);
    leveldb::Slice key_slice(reinterpret_cast<const char*>(stream_key_.GetData().data()),
                             stream_key_.GetData().size());
    batch_.Delete(key_slice);
    ClearStreams();
  }

  void Remove(const uint8_t* key, size_t key_size) {
    leveldb::Slice key_slice(reinterpret_cast<const char*>(key), key_size);
    batch_.Delete(key_slice);
  }

  void Clear() {
    batch_.Clear();
    ClearStreams();
  }

 private:
  void ClearStreams() noexcept {
    stream_key_.Clear();
    stream_val_.Clear();
  }

  leveldb::WriteBatch batch_;
  Serializer stream_key_;
  Serializer stream_val_;

  friend class Database;
};

class DbIterator {
 public:
  bool IsValid() { return (it_ && it_->Valid()); };

  template<class K>
  bool Key(K& key) {
    if (!IsValid()) return false;
    auto key_slice = it_->key();
    Unserializer u(key_slice.data(), key_slice.size());
    return u.Get(key);
  }

  template<class V>
  bool Value(V& value) {
    if (!IsValid()) return false;
    auto value_slice = it_->value();
    Unserializer u(value_slice.data(), value_slice.size());
    return u.Get(value);
  }

  void operator++() {
    if (IsValid()) {
      it_->Next();
    }
  }

  void operator++(int) { operator++(); }

 private:
  DbIterator(leveldb::Iterator* it) : it_(it) {}

  std::shared_ptr<leveldb::Iterator> it_;

  friend class Database;
};

class Database {
 public:
  Database(const std::string& path, bool wipe = false) {
    leveldb::Options options;
    options.create_if_missing = true;

    if (wipe) {
      HandleError(leveldb::DestroyDB(path, options));
    }

    HandleError(leveldb::DB::Open(options, path, &db_ptr_));
  }

  ~Database() {
    delete db_ptr_;
    db_ptr_ = nullptr;
  }

  template<class K, class V>
  void Read(const K& key, V& value) const {
    Serializer s;
    s.Put(key);
    leveldb::Slice key_slice(reinterpret_cast<const char*>(s.GetData().data()),
                             s.GetData().size());

    std::string str_value;
    HandleError(db_ptr_->Get(read_options_, key_slice, &str_value));
    Unserializer is(str_value.data(), str_value.size());
    is.Get(value);
  }

  template<class V>
  void Read(const uint8_t* key, size_t key_size, V& value) const {
    leveldb::Slice key_slice(reinterpret_cast<const char*>(key), key_size);

    std::string str_value;
    HandleError(db_ptr_->Get(read_options_, key_slice, &str_value));
    Unserializer is(str_value.data(), str_value.size());
    is.Get(value);
  }

  template<class K, class V>
  void Write(const K& key, const V& value) {
    WriteBatch batch;
    batch.Write(key, value);
    Write(batch);
  }

  template<class V>
  void Write(const uint8_t* key, size_t key_size, const V& value) {
    WriteBatch batch;
    batch.Write(key, key_size, value);
    Write(batch);
  }

  template<class K>
  bool Exists(const K& key) const {
    Serializer s;
    s.Put(key);
    leveldb::Slice key_slice(reinterpret_cast<const char*>(s.GetData().data()),
                             s.GetData().size());

    std::string str_value;
    auto status = db_ptr_->Get(read_options_, key_slice, &str_value);
    return status.ok();
  }

  template<class K>
  void Remove(const K& key) {
    WriteBatch batch;
    batch.Remove(key);
    Write(batch);
  }

  void Remove(uint8_t* key, size_t key_size) {
    WriteBatch batch;
    batch.Remove(key, key_size);
    Write(batch);
  }

  void Write(WriteBatch& batch) {
    HandleError(db_ptr_->Write(leveldb::WriteOptions(), &batch.batch_));
  }

  DbIterator begin() {
    DbIterator it(db_ptr_->NewIterator(read_options_));
    it.it_->SeekToFirst();
    return it;
  }

 private:
  void HandleError(const leveldb::Status& status) const {
    if (status.ok()) return;
    throw std::runtime_error(status.ToString());
  }

  leveldb::DB* db_ptr_;
  leveldb::ReadOptions read_options_;
};
} // namespace net
#endif // NET_DATABASE_H
