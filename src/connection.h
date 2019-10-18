#ifndef NET_CONNECTION_H
#define NET_CONNECTION_H

#include <memory>

#include "common.h"

namespace net {

class Host;

class Connection : public std::enable_shared_from_this<Connection> {
 public:
  using Ptr = std::shared_ptr<Connection>;

  static auto Create(Host& h, ba::io_context& io) {
    return Ptr(new Connection(h, io));
  }

  static auto Create(Host& h, bi::tcp::socket&& s) {
    return Ptr(new Connection(h, std::move(s)));
  }

  auto& Socket() { return socket_; }

  ~Connection() {
    Close();
  }

  void StartRead();
  void Close();

 private:
  Connection(Host& h, ba::io_context& io)
      : host_(h), socket_(io) {}
  Connection(Host& h, bi::tcp::socket&& s)
      : host_(h), socket_(std::move(s)) {}

  Host& host_;
  bi::tcp::socket socket_;
  Packet packet_;
};

} // namespace net
#endif // NET_CONNECTION_H
