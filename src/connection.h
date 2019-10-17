#ifndef NET_CONNECTION_H
#define NET_CONNECTION_H

#include <memory>

#include "common.h"

namespace net {

class Connection : public std::enable_shared_from_this<Connection> {
 public:
  using Ptr = std::shared_ptr<Connection>;

  static auto Create(ba::io_context& io) {
    return Ptr(new Connection(io));
  }

  static auto Create(bi::tcp::socket&& s) {
    return Ptr(new Connection(std::move(s)));
  }

  auto& Socket() { return socket_; }

  ~Connection() {
    Close();
  }

  void Close() {
    try {
      boost::system::error_code ec;
      socket_.shutdown(bi::tcp::socket::shutdown_both, ec);
      if (socket_.is_open()) socket_.close();
    } catch (...) {}
  }

 private:
  Connection(ba::io_context& io) : socket_(io) {}
  Connection(bi::tcp::socket&& s) : socket_(std::move(s)) {}

  bi::tcp::socket socket_;
};

} // namespace net
#endif // NET_CONNECTION_H
