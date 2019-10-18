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
            if (er || len != Packet::kHeaderSize) {
              LOG(DEBUG) << "Error while receive header, "
                         << er.value() << ", " << er.message();
              return;
            }

            Unserializer u(packet_.data.data(), Packet::kHeaderSize);
            if (!packet_.GetHeader(u)) {
              LOG(DEBUG) << "Invalid header reseived";
              return;
            }

            packet_.data.resize(packet_.header.data_size);
            ba::async_read(socket_, ba::buffer(packet_.data, packet_.header.data_size),
                    [this, self](const boost::system::error_code& er, size_t len) {
                      if (er || len != packet_.header.data_size) {
                        LOG(DEBUG) << "Error while receive message, "
                                   << er.value() << ", " << er.message();
                        return;
                      }

                      host_.OnPacketReceived(std::move(packet_));
                    });
         });
}

} // namespace net
