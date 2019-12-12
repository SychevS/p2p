#ifndef NET_MURMURHASH2_H
#define NET_MURMURHASH2_H

#include <cinttypes>

namespace net {
uint32_t MurmurHash2(const uint8_t* key, unsigned int len);
} // namespace net
#endif // NET_MURMURHASH2_H
