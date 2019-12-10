#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>

#include "localip.h"

namespace net {

std::vector<std::string> getLocalIp4() {
    std::vector<std::string> result;
    struct ifaddrs *ifa, *ifap;
    if (getifaddrs(&ifa) != 0) return result;

    char buf[255];
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
            result.push_back(std::string(buf));
        }
    }
    freeifaddrs(ifa);
    return result;
}
}  // nemespace net
