#include "serialization.h"

namespace net {

void Serializer::Put(const std::string& s) {
  Put(s.size());
  buffer_.insert(buffer_.end(), s.cbegin(), s.cend());
}

void Serializer::Put(const uint8_t* data, size_t size) {
  buffer_.insert(buffer_.end(), data, data + size);
}

void Serializer::Put(const std::vector<uint8_t>& data) {
  Put(data.size());
  Put(data.data(), data.size());
}

bool Unserializer::Get(std::string& s) {
  size_t size;
  if (!Get(size) || size > size_) {
    return false;
  }

  auto ptr = reinterpret_cast<const char**>(&data_);

  s.assign(*ptr, *ptr + size);
  *ptr += size;
  size_ -= size;
  return true;
}

bool Unserializer::Get(uint8_t* data, size_t size) {
  if (size > size_) return false;

  auto ptr = reinterpret_cast<const uint8_t**>(&data_);
  std::copy(*ptr, *ptr + size, data);
  *ptr += size;
  size_ -= size;
  return true;
}

bool Unserializer::Get(std::vector<uint8_t>& data) {
  size_t size;
  if (!Get(size)) return false;
  data.resize(size);
  return Get(data.data(), data.size());
}

} // namespace net
