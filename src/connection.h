#ifndef NET_CONNECTION_H
#define NET_CONNECTION_H

#include <deque>
#include <memory>
#include <string>

#include "common.h"

namespace net {

class Host;

class Connection : public std::enable_shared_from_this<Connection> {
 public:
  using Ptr = std::shared_ptr<Connection>;
  using Endpoint = bi::tcp::endpoint;

  enum DropReason : uint8_t {
    kTimeout,
    kReadError,
    kWriteError,
    kProtocolCorrupted,
    kConnectionError
  };

  static std::string DropReasonToString(DropReason);

  static auto Create(Host& h, ba::io_context& io) {
    return Ptr(new Connection(h, io));
  }

  static auto Create(Host& h, ba::io_context& io, bi::tcp::socket&& s) {
    return Ptr(new Connection(h, io, std::move(s)));
  }

  void Connect(const Endpoint&, Packet&& reg_pack);

  ~Connection() { Close(); }
  void Close();

  void Send(Packet&&);
  void StartRead();

  bool IsActive() const noexcept { return active_; }
  bool IsConnected() const;

  Endpoint GetEndpoint() const noexcept { return socket_.remote_endpoint(); }

 private:
  constexpr static uint16_t kTimeoutSeconds = 5;

  // active connection
  Connection(Host&, ba::io_context&);
  // passive connection
  Connection(Host&, ba::io_context&, bi::tcp::socket&&);

  void StartWrite();
  bool CheckRead(const boost::system::error_code&, size_t expected, size_t len);
  void Drop(DropReason);

  void ResetTimer();

  Host& host_;
  bi::tcp::socket socket_;
  Packet packet_;

  Mutex send_mux_;
  std::deque<ByteVector> send_queue_;

  std::atomic<bool> registation_passed_ = false;
  std::atomic<bool> dropped_ = false;
  std::atomic<bool> timer_started_ = false;
  const bool active_;

  NodeId remote_node_;
  ba::deadline_timer deadline_;
};
} // namespace net
#endif // NET_CONNECTION_H
