# P2P
Kademlia based p2p network.

### Installation

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
