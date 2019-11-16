#ifndef NET_CONNECTION_H
#define NET_CONNECTION_H

#include <deque>
#include <memory>

#include "common.h"

namespace net {

class Host;

class Connection : public std::enable_shared_from_this<Connection> {
 public:
  using Ptr = std::shared_ptr<Connection>;
  using Endpoint = bi::tcp::endpoint;

  static auto Create(Host& h, ba::io_context& io) {
    return Ptr(new Connection(h, io));
  }
  static auto Create(Host& h, bi::tcp::socket&& s) {
    return Ptr(new Connection(h, std::move(s)));
  }

  void Connect(const Endpoint&, Packet&& reg_pack);

  ~Connection() { Close(); }
  void Close();

  void Send(Packet&&);
  void StartRead();

 private:
  Connection(Host& h, ba::io_context& io) : host_(h), socket_(io) {}
  Connection(Host& h, bi::tcp::socket&& s) : host_(h), socket_(std::move(s)) {}

  void StartWrite();
  bool CheckRead(const boost::system::error_code&, size_t expected, size_t len);

  Host& host_;
  bi::tcp::socket socket_;
  Packet packet_;

  Mutex send_mux_;
  std::deque<ByteVector> send_queue_;
};
} // namespace net
#endif // NET_CONNECTION_H
