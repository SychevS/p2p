#ifndef NET_TYPES_H
#define NET_TYPES_H

#include <array>
#include <cinttypes>
#include <mutex>

namespace net {

template<size_t SIZE>
using ByteArray = std::array<uint8_t, SIZE>;

using Mutex = std::mutex;
using Guard = std::lock_guard<Mutex>;
using UniqueGuard = std::unique_lock<Mutex>;

} // namespace net
#endif // NET_TYPES_H
