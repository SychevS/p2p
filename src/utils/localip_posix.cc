#include "localip.h"

#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace net {

std::set<bi::address> GetLocalIp4() {
  std::set<bi::address> result;
  struct ifaddrs *ifa, *ifap;
  char buf[255];

  if (getifaddrs(&ifa) != 0)
    return result;
  for (ifap = ifa; ifap != nullptr; ifap = ifap->ifa_next) {
    if (ifap->ifa_addr == nullptr)
      continue;
    if (ifap->ifa_flags & IFF_LOOPBACK)
      continue;
    if (!(ifap->ifa_flags & IFF_UP))
      continue;
    if (ifap->ifa_addr->sa_family != AF_INET)
      continue;

    int ret = getnameinfo(ifap->ifa_addr, sizeof(struct sockaddr_in),
      buf, sizeof(buf), nullptr, 0, NI_NUMERICHOST);
    if (ret == 0) {
      result.insert(bi::make_address(std::string(buf)));
    }
  }
  freeifaddrs(ifa);
  return result;
}
}  // nemespace net
