#include "connection.h"

namespace net {

void Connection::Close() {
  try {
    boost::system::error_code ec;
    socket_.shutdown(bi::tcp::socket::shutdown_both, ec);
    if (socket_.is_open()) socket_.close();
  } catch (...) {}
}

void Connection::StartRead() {

}

} // namespace net
