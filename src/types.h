#ifndef NET_TYPES_H
#define NET_TYPES_H

#include <array>
#include <cinttypes>
#include <mutex>
#include <vector>

namespace net {

template<size_t SIZE>
using ByteArray = std::array<uint8_t, SIZE>;

using ByteVector = std::vector<uint8_t>;

using Mutex = std::mutex;
using Guard = std::lock_guard<std::mutex>;

} // namespace net
#endif // NET_TYPES_H
