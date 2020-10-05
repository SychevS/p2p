# P2P
Kademlia based p2p network.

### Installation

P2P requires [boost](https://www.boost.org/) 1.68+ to compile.

```sh
$ git clone https://github.com/SychevS/p2p.git
$ cd p2p
$ git submodule update --init --recursive
$ mkdir build
$ cd build
$ cmake -DBOOST_ROOT=${BOOST_ROOT} -DCMAKE_INSTALL_PREFIX=${P2P_INSTALL_ROOT} ..
$ cmake --build . --target install
```
