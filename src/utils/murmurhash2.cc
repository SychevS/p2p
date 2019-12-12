#include "murmurhash2.h"

namespace net {

uint32_t MurmurHash2(const uint8_t* key, unsigned int len) {
  constexpr uint32_t m = 0x5bd1e995;
  constexpr uint32_t seed = 0;
  constexpr int r = 24;

  unsigned int h = seed ^ len;

  const uint8_t* data = key;
  unsigned int k;

  while (len >= 4) {
      k  = data[0];
      k |= data[1] << 8;
      k |= data[2] << 16;
      k |= data[3] << 24;

      k *= m;
      k ^= k >> r;
      k *= m;

      h *= m;
      h ^= k;

      data += 4;
      len -= 4;
  }

  switch (len) {
    case 3:
      h ^= data[2] << 16;
      [[fallthrough]];
    case 2:
      h ^= data[1] << 8;
      [[fallthrough]];
    case 1:
      h ^= data[0];
      h *= m;
  };

  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;

  return h;
}
} // namespace net
