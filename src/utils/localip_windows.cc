#include "localip.h"

#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace net {

std::set<bi::address> GetLocalIp4() {
  std::set<bi::address> result;

  PIP_ADAPTER_ADDRESSES pAddresses = NULL;
  ULONG outBufLen = 15000;
  ULONG Iterations = 0;
  DWORD dwRetVal = 0;

  do {
    pAddresses = (IP_ADAPTER_ADDRESSES*)new char[outBufLen];
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    dwRetVal =
      GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &outBufLen);

    if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
      delete[] (char *)pAddresses;
      pAddresses = NULL;
    }
    else {
      break;
    }
    Iterations++;
  } while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (Iterations < 3));

  PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
  while (pCurrAddresses) {
    do {
      if (pCurrAddresses->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
        break;
      if (pCurrAddresses->OperStatus == IfOperStatusDown)
        break;

      PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
      if (pUnicast != NULL) {
        char buf[255];
        while (pUnicast) {
          int ret = getnameinfo(pUnicast->Address.lpSockaddr, sizeof(struct sockaddr),
            buf, sizeof(buf), nullptr, 0, NI_NUMERICHOST);
          if (ret == 0) {
              result.insert(bi::make_address(std::string(buf)));
          }
          pUnicast = pUnicast->Next;
        }
      }
    } while (false);
    pCurrAddresses = pCurrAddresses->Next;
  }

  if (pAddresses) {
    delete[] (char *)pAddresses;
  }
  return result;
}
}  // nemespace net
