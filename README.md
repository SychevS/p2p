# P2P
[Kademlia](https://en.wikipedia.org/wiki/Kademlia) based p2p network.

## Installation

P2P requires to compile:
 - [boost](https://www.boost.org/) 1.68+
 - [cmake](https://cmake.org/) 3.13+
 - compiler with C++17 support

```sh
$ git clone https://github.com/SychevS/p2p.git
$ cd p2p
$ git submodule update --init --recursive
$ mkdir build
$ cd build
$ cmake -DBOOST_ROOT=${BOOST_ROOT} -DCMAKE_INSTALL_PREFIX=${P2P_INSTALL_ROOT} ..
$ cmake --build . --target install
```

## Basic usage

Lets suppose that we are going to connect only two nodes.

First node:
```cpp
#include <p2p_network.h>

using namespace net;

NodeId GenerateRandomId();
NodeId GetFirstNodeId();
NodeId GetSecondNodeId();

void StartMainEventLoop();

class Host : public EventHandler {
  void OnNodeDiscovered(const NodeId&) {
    std::cout << "Node discovered" << std::endl;
  }

  void OnNodeRemoved(const NodeId&) {}
  void OnMessageReceived(const NodeId& from, ByteVector&& message) {}

  void OnFragmentFound(const FragmentId&, ByteVector&& value) {}
  void OnFragmentNotFound(const FragmentId& id) {}

  FragmentId GetFragmentId(const ByteVector& fragment) {
    return GenerateRandomId();
  }
};

int main() {
  net::ManagerConfig config {
    GetFirstNodeId(),
    6100,
    {{GetSecondNodeId(), "127.0.0.1", 6101}},
    false
  };
  Host host;

  Manager netman(config, host);
  netman.Start();

  StartMainEventLoop();
}
```

Main of second node:
```cpp
int main() {
  net::ManagerConfig config {
    GetSecondNodeId(),
    6101,
    {{GetFirstNodeId(), "127.0.0.1", 6100}},
    false
  };
  Host host;

  Manager netman(config, host);
  netman.Start();

  StartMainEventLoop();
}
```
Now after discovery these two nodes will be able to communicate with each other using Manager's class interface.
For more details see include/p2p_network.h
