#ifndef NET_UDP_H
#define NET_UDP_H

#include <atomic>
#include <deque>
#include <memory>
#include <utility>

#include "common.h"
#include "log.h"
#include "types.h"

namespace net {

class UdpDatagram {
 public:
  UdpDatagram(const bi::udp::endpoint& ep,
              ByteVector data = {}) : ep_(ep), data_(std::move(data)) {}

  auto& Data() noexcept { return data_; }
  const auto& Data() const noexcept { return data_; }
  const auto& Endpoint() const noexcept { return ep_; }

 private:
  bi::udp::endpoint ep_;
  ByteVector data_;
};

// Interface which socket's owner must implement.
class UdpSocketEventHandler {
 public:
  virtual ~UdpSocketEventHandler() = default;
  virtual void OnPacketReceived(const bi::udp::endpoint& from, const ByteVector& data) = 0;
  virtual void OnSocketClosed(const boost::system::error_code&) = 0;
};

template <size_t MaxDatagramSize>
class UdpSocket : public std::enable_shared_from_this<UdpSocket<MaxDatagramSize>> {
 public:
  static_assert(MaxDatagramSize < 65507, "UDP datagrams cannot be larger than 65507 bytes");

  using Ptr = std::shared_ptr<UdpSocket<MaxDatagramSize>>;

  static auto Create(ba::io_context& io, uint16_t port, UdpSocketEventHandler& host) {
      return Ptr(new UdpSocket<MaxDatagramSize>(io, port, host));
  }

  void Open();

  template<class Datagram>
  bool Send(Datagram&& datagram);

  bool IsOpen() const noexcept { return !closed_; }

  void Close() { CloseWithError(ba::error::connection_reset); }

 private:
  UdpSocket(ba::io_context& io, uint16_t port, UdpSocketEventHandler& host)
      : listen_ep_(bi::udp::v4(), port), socket_(io), host_(host) {
    started_.store(false);
    closed_.store(true);
  }

  void CloseWithError(const boost::system::error_code& ec);
  void StartRead();
  void StartWrite();

  bi::udp::endpoint listen_ep_;
  bi::udp::endpoint recv_ep_;

  Mutex send_mux_;
  std::deque<UdpDatagram> send_queue_;
  ByteArray<MaxDatagramSize> recv_buf_;

  bi::udp::socket socket_;

  UdpSocketEventHandler& host_;

  std::atomic<bool> started_;
  std::atomic<bool> closed_;
};

template<size_t MaxDatagramSize>
void UdpSocket<MaxDatagramSize>::Open() {
  if (started_.load()) return;
  started_.store(true);

  socket_.open(bi::udp::v4());
  try {
    socket_.bind(listen_ep_);
  } catch (std::exception& e) {
    LOG(ERROR) << "Can't bind socket to port " << listen_ep_.port()
               << ", reason " << e.what();
    started_.store(false);
    return;
  }

  {
   // don't send stale messages on restart
   Guard g(send_mux_);
   send_queue_.clear();
  }

  closed_.store(false);
  StartRead();
}

template<size_t MaxDatagramSize>
void UdpSocket<MaxDatagramSize>::StartRead() {
  if (closed_.load()) return;

  Ptr self(UdpSocket<MaxDatagramSize>::shared_from_this());
  socket_.async_receive_from(ba::buffer(recv_buf_), recv_ep_,
          [this, self](const boost::system::error_code& ec, size_t len) {
            if (closed_.load()) {
              return CloseWithError(ec);
            }

            if (ec) {
              LOG(ERROR) << "Receive of UDP datagram failed. " << ec.value()
                         << ", " << ec.message();
            }

            if (len) {
                host_.OnPacketReceived(recv_ep_, ByteVector(recv_buf_.begin(), recv_buf_.end()));
            }

            StartRead();
          });
}

template<size_t MaxDatagramSize>
template<class Datagram>
bool UdpSocket<MaxDatagramSize>::Send(Datagram&& datagram) {
  if (closed_.load()) return false;

  Guard g(send_mux_);
  send_queue_.emplace_back(std::forward<Datagram>(datagram));

  if (send_queue_.size()) {
    StartWrite();
  }

  return true;
}

template<size_t MaxDatagramSize>
void UdpSocket<MaxDatagramSize>::StartWrite() {
  if (closed_.load()) return;

  const auto& datagram = send_queue_[0];
  Ptr self(UdpSocket<MaxDatagramSize>::shared_from_this());

  socket_.async_send_to(ba::buffer(datagram.Data()), datagram.Endpoint(),
          [this, self](const boost::system::error_code& ec, size_t) {
            if (closed_.load()) {
              return CloseWithError(ec);
            }

            if (ec) {
              LOG(ERROR) << "Send of UDP datagram failed. "
                         << ec.value() << ", " << ec.message();
            }

            {
             Guard g(send_mux_);
             send_queue_.pop_front();
            }

            if (send_queue_.empty()) return;

            StartWrite();
          });
}

template<size_t MaxDatagramSize>
void UdpSocket<MaxDatagramSize>::CloseWithError(const boost::system::error_code& ec) {
  if (!started_ && closed_ && !socket_.is_open()) return;

  if (ec != boost::system::error_code()) return;

  // prevent concurrent disconnect
  bool expected = true;
  if (!started_.compare_exchange_strong(expected, false)) return;

  bool was_closed = closed_.load();
  closed_.store(true);

  boost::system::error_code erc;
  socket_.shutdown(bi::udp::socket::shutdown_both, erc);
  socket_.close();

  if (was_closed) return;

  host_.OnSocketClosed(ec);
}

} // namespace net
#endif // NET_UDP_H
