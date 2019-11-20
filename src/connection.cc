#include "connection.h"

#include "host.h"

namespace net {

void Connection::Close() {
  try {
    boost::system::error_code ec;
    socket_.shutdown(bi::tcp::socket::shutdown_both, ec);
    if (socket_.is_open()) socket_.close();
  } catch (...) {}
}

void Connection::StartRead() {
  Ptr self(shared_from_this());
  packet_.data.resize(Packet::kHeaderSize);

  ba::async_read(socket_, ba::buffer(packet_.data, Packet::kHeaderSize),
          [this, self](const boost::system::error_code& er, size_t len) {
            if (!CheckRead(er, Packet::kHeaderSize, len)) {
              return;
            }

            Unserializer u(packet_.data.data(), Packet::kHeaderSize);
            if (!packet_.GetHeader(u)) {
              LOG(DEBUG) << "Invalid header reseived";
              Close();
              return;
            }

            packet_.data.resize(packet_.header.data_size);
            ba::async_read(socket_, ba::buffer(packet_.data, packet_.header.data_size),
                    [this, self](const boost::system::error_code& er, size_t len) {
                      if (!CheckRead(er, packet_.header.data_size, len)) {
                        return;
                      }

                      bool is_reg = packet_.IsRegistration();

                      if (!registation_passed_) {
                        if (!is_reg || !host_.OnConnected(packet_.header.sender, self)) {
                          Close();
                          return;
                        } else {
                          registation_passed_ = true;
                          StartRead();
                          return;
                        }
                      }

                      if (is_reg) {
                        LOG(DEBUG) << "Reg packet recieved, when registartion passed.";
                        Close();
                        return;
                      }

                      host_.OnPacketReceived(std::move(packet_));
                      packet_ = Packet();
                      StartRead();
                    }
            );
         }
  );
}

bool Connection::CheckRead(const boost::system::error_code& er, size_t expected, size_t len) {
  if (er && er.category() != ba::error::get_misc_category() && er.value() != ba::error::eof) {
    LOG(DEBUG) << "Error reading " << er.value() << ", " << er.message();
    Close();
    return false;
  }

  if (er && len < expected) {
    LOG(DEBUG) << "Error reading " << er.value() << ", " << er.message()
               << ", len " << len;
    Close();
    return false;
  }

  if (len != expected) {
    LOG(ERROR) << "Wrong packet length";
    Close();
    return false;
  }

  return true;
}

void Connection::Send(Packet&& pack) {
  Guard g(send_mux_);
  Serializer s;
  s.Put(pack);
  send_queue_.push_back(s.GetData());

  if (send_queue_.size() == 1) {
    StartWrite();
  }
}

void Connection::StartWrite() {
  Ptr self(shared_from_this());
  ba::async_write(socket_, ba::buffer(send_queue_[0]),
      [this, self](const boost::system::error_code& err, size_t /* written length */) {
        if (err) {
          LOG(DEBUG) << "Cannot send packet, reason " << err.value()
                     << ", " << err.message();
        }

        Guard g(send_mux_);
        send_queue_.pop_front();

        if (send_queue_.empty()) return;

        StartWrite();
      }
  );
}

void Connection::Connect(const Endpoint& ep, Packet&& reg_pack) {
  Ptr self(shared_from_this());
  {
   Guard g(send_mux_);
   Serializer s;
   s.Put(reg_pack);
   send_queue_.push_back(s.GetData());
  }
  socket_.async_connect(ep, [this, self, id = reg_pack.header.receiver](const boost::system::error_code& err) {
                              if (err) {
                                LOG(DEBUG) << "Cannot connect to peer, reason " << err.value()
                                           << ", " << err.message();
                                return;
                              }

                              StartWrite();
                              StartRead();
                            }
  );
}
} // namespace net
