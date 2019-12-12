#ifndef NET_LOCALIP_H
#define NET_LOCALIP_H

#include <set>
#include <string>

#include "../common.h"

namespace net {
std::set<bi::address> GetLocalIp4();
}  // namespace net

#endif // NET_LOCALIP_H
