#ifndef NET_SERIALIZATION_H
#define NET_SERIALIZATION_H

#include <algorithm>
#include <array>
#include <cinttypes>
#include <string>
#include <type_traits>
#include <vector>

namespace net {

class Serializer {
 public:
  using Data = std::vector<uint8_t>;

  Serializer() = default;

  const Data& GetData() const noexcept { return buffer_; }

  template<class T,
           class = std::enable_if_t<std::is_class<T>::value>>
  decltype(auto) Put(const T& data) {
    return data.Put(*this);
  }

  template<class Integer,
           class = std::enable_if_t<std::is_integral<Integer>::value>>
  void Put(Integer data) {
    auto ptr = reinterpret_cast<uint8_t*>(&data);
    buffer_.insert(buffer_.end(), ptr, ptr + sizeof(Integer));
  }

  void Put(const std::string&);
  void Put(const uint8_t*, size_t);
  void Put(const std::vector<uint8_t>&);

  template<size_t SIZE>
  void Put(const std::array<uint8_t, SIZE>& data) {
    Put(data.data(), SIZE);
  }

  void Clear() noexcept {
    buffer_.clear();
  }

 private:
  Data buffer_;
};

class Unserializer {
 public:
  Unserializer(const void* data, size_t size) : data_(data), size_(size) {}

  template<class T,
           class = std::enable_if_t<std::is_class<T>::value>>
  decltype(auto) Get(T& data) {
    return data.Get(*this);
  }

  template<class Integer,
           class = std::enable_if_t<std::is_integral<Integer>::value>>
  bool Get(Integer& integer) {
    if (size_ < sizeof(Integer)) return false;

    auto ptr = reinterpret_cast<const uint8_t**>(&data_);
    std::copy(*ptr, *ptr + sizeof(Integer), reinterpret_cast<uint8_t*>(&integer));
    *ptr += sizeof(Integer);
    size_ -= sizeof(Integer);
    return true;
  }

  bool Get(std::string&);
  bool Get(uint8_t*, size_t);
  bool Get(std::vector<uint8_t>&);

  template<size_t SIZE>
  bool Get(std::array<uint8_t, SIZE>& data) {
    return Get(data.data(), SIZE);
  }

 private:
  const void* data_;
  size_t size_;
};
} // namespace net
#endif // NET_SERIALIZATION_H
