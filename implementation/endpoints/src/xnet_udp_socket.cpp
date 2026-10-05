#include <cstring>
#include <algorithm>
#include <limits>
#include <vector>
#include <optional>

#include <boost/asio/defer.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/buffer.hpp>

#include "../include/xnet_socket_helper.hpp"
#include "../include/xnet_udp_socket.hpp"
#include "../include/xnet_error.hpp"
#include "../include/xnet_api.hpp"

#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX "[XNET][udp]"

#define SOCKET_ERROR_VALUE -1

namespace vsomeip_v3 {

namespace {

void post_receive_from_completion(boost::asio::io_context& _io, udp_socket::rw_handler _handler, boost::system::error_code const& _ec,
                                  std::size_t _bytes, boost::asio::mutable_buffer _caller_buffer,
                                  boost::asio::ip::udp::endpoint* _caller_remote, std::shared_ptr<std::vector<std::uint8_t>> _received_data,
                                  std::shared_ptr<boost::asio::ip::udp::endpoint> _source_endpoint) {
    auto its_executor = boost::asio::get_associated_executor(_handler, _io.get_executor());
    boost::asio::post(its_executor,
                      [handler = std::move(_handler), ec = _ec, bytes = _bytes, caller_buffer = _caller_buffer,
                       caller_remote = _caller_remote, received_data = std::move(_received_data),
                       source_endpoint = std::move(_source_endpoint)]() mutable {
                          std::size_t its_bytes = bytes;
                          if (!ec) {
                              its_bytes = std::min(its_bytes, caller_buffer.size());
                              if (its_bytes > 0 && caller_buffer.data() != nullptr && !received_data->empty()) {
                                  std::memcpy(caller_buffer.data(), received_data->data(), its_bytes);
                              }
                              if (caller_remote != nullptr) {
                                  *caller_remote = *source_endpoint;
                              }
                          } else {
                              its_bytes = 0;
                          }

                          handler(ec, its_bytes);
                      });
}

} // namespace

xnet_udp_socket::xnet_udp_socket(boost::asio::io_context& _io, nxIpStackRef_t xnet_stack)
    : socket_(nxINVALID_SOCKET),
      io_context_(_io),
      xnet_stack_(xnet_stack),
      is_ipv6_(false),
      non_blocking_mode_(false),
      cancel_epoch_(0) {
}

xnet_udp_socket::~xnet_udp_socket() {
    boost::system::error_code ec;
    close(ec);
}

bool xnet_udp_socket::is_open() const {
    return socket_ != nxINVALID_SOCKET;
}

int xnet_udp_socket::native_handle() {
    return static_cast<int>(socket_);
}

void xnet_udp_socket::open(boost::asio::ip::udp::endpoint::protocol_type pt, boost::system::error_code& ec) {
    if (is_open()) { 
        close(ec); 
        if (ec) {
            return;
        }
    }

    is_ipv6_ = (pt == boost::asio::ip::udp::v6());

    socket_ = xnet_api::nxsocket(xnet_stack_, is_ipv6_ ? nxAF_INET6 : nxAF_INET, nxSOCK_DGRAM, nxIPPROTO_UDP);

    if (socket_ == nxINVALID_SOCKET) {
        ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    // Set non-blocking mode (required for vsomeip)
    native_non_blocking(true, ec);
    if (ec) {
        boost::system::error_code its_close_error;
        close(its_close_error);
        return;
    }

    ec.clear();
}

void xnet_udp_socket::bind(boost::asio::ip::udp::endpoint const& ep, boost::system::error_code& ec) {
    if (!is_open()) { 
        ec = boost::asio::error::bad_descriptor; 
        return; 
    }

    // Validate address family matches the opened socket; reject mismatches early
    const bool ep_is_v6 = ep.address().is_v6();
    if (ep_is_v6 != is_ipv6_) {
        ec = boost::asio::error::make_error_code(boost::asio::error::address_family_not_supported);
        return;
    }

    nxsockaddr_storage its_storage{};
    nxsocklen_t its_length = 0;
    if (!xnet_socket_helper::endpoint_to_native(ep.address(), ep.port(), its_storage, its_length, ec)) {
        return;
    }

    if (xnet_api::nxbind(socket_, reinterpret_cast<nxsockaddr*>(&its_storage), its_length) == SOCKET_ERROR_VALUE) {
        ec = xnet_socket_helper::get_xnet_error();
        return;
    }
    
    ec.clear();
}

void xnet_udp_socket::close(boost::system::error_code& ec) { 
    cancel_epoch_.fetch_add(1, std::memory_order_relaxed);

    if (is_open()) {
        if (xnet_api::nxclose(socket_) == SOCKET_ERROR_VALUE) {
            ec = xnet_socket_helper::get_xnet_error();
            socket_ = nxINVALID_SOCKET;
            non_blocking_mode_ = false;
            stop_worker_threads();
            return;
        }
        socket_ = nxINVALID_SOCKET;
        non_blocking_mode_ = false;
    }
    ec.clear();
    stop_worker_threads();
}

bool xnet_udp_socket::native_non_blocking() const { 
    if (!is_open()) {
        return false;
    }

    int mode = 0;
    nxsocklen_t opt_len = static_cast<nxsocklen_t>(sizeof(mode));
    if (xnet_api::nxgetsockopt(socket_, nxSOL_SOCKET, nxSO_NONBLOCK, &mode, &opt_len) != SOCKET_ERROR_VALUE
        && opt_len >= static_cast<nxsocklen_t>(sizeof(mode))) {
        return mode != 0;
    }
    return non_blocking_mode_;
}

void xnet_udp_socket::native_non_blocking(bool mode, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = mode ? 1 : 0;
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_NONBLOCK, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = xnet_socket_helper::get_xnet_error();
        return;
    }
    
    non_blocking_mode_ = mode;
    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::udp::socket::reuse_address ra, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = ra.value() ? 1 : 0;
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_REUSEADDR, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::udp::socket::broadcast broad, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    (void)broad;

    xnet_socket_helper::make_unsupported_option_warning(VSOMEIP_LOG_PREFIX, "broadcast", "XNET stack does not expose nxSO_BROADCAST");

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::udp::socket::receive_buffer_size rx_size, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    int opt = rx_size.value();
    if (xnet_api::nxsetsockopt(socket_, nxSOL_SOCKET, nxSO_RCVBUF, &opt, static_cast<nxsocklen_t>(sizeof(opt))) == SOCKET_ERROR_VALUE) {
        ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::multicast::join_group join, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    if (is_ipv6_) {
        const auto protocol = boost::asio::ip::udp::v6();
        const auto option_size = join.size(protocol);
        const auto option_data = join.data(protocol);
        if (option_size < sizeof(nxipv6_mreq)) {
            ec = boost::asio::error::address_family_not_supported;
            return;
        }
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IPV6, nxIPV6_JOIN_GROUP, option_data, static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = xnet_socket_helper::get_xnet_error();
            return;
        }
    } else {
        const auto protocol = boost::asio::ip::udp::v4();
        const auto option_size = join.size(protocol);
        const auto option_data = join.data(protocol);
        if (option_size < sizeof(nxip_mreq)) {
            ec = boost::asio::error::address_family_not_supported;
            return;
        }
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IP, nxIP_ADD_MEMBERSHIP, option_data, static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = xnet_socket_helper::get_xnet_error();
            return;
        }
    }

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::multicast::leave_group leave, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    if (is_ipv6_) {
        const auto protocol = boost::asio::ip::udp::v6();
        const auto option_size = leave.size(protocol);
        const auto option_data = leave.data(protocol);
        if (option_size < sizeof(nxipv6_mreq)) {
            ec = boost::asio::error::address_family_not_supported;
            return;
        }
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IPV6, nxIPV6_LEAVE_GROUP, option_data, static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = xnet_socket_helper::get_xnet_error();
            return;
        }
    } else {
        const auto protocol = boost::asio::ip::udp::v4();
        const auto option_size = leave.size(protocol);
        const auto option_data = leave.data(protocol);
        if (option_size < sizeof(nxip_mreq)) {
            ec = boost::asio::error::address_family_not_supported;
            return;
        }
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IP, nxIP_DROP_MEMBERSHIP, option_data, static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = xnet_socket_helper::get_xnet_error();
            return;
        }
    }

    ec.clear();
}

void xnet_udp_socket::set_option(boost::asio::ip::multicast::outbound_interface outbound, boost::system::error_code& ec) { 
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    if (is_ipv6_) {
        const auto protocol = boost::asio::ip::udp::v6();
        const auto option_size = outbound.size(protocol);
        if (option_size != sizeof(unsigned int)) {
            ec = boost::asio::error::address_family_not_supported;
            return;
        }
        int32_t if_index = static_cast<int32_t>(*reinterpret_cast<const unsigned int*>(outbound.data(protocol)));
        if (if_index == 0) {
            nxVirtualInterface_t* interfaces = nullptr;
            const auto status = ::nxIpStackGetInfo(xnet_stack_, nxIPSTACK_INFO_ID, &interfaces);
            if (status == 0) {
                for (auto* interface = interfaces; interface != nullptr; interface = interface->nextVirtualInterface) {
                    if (interface->operationalStatus == nxOPERATIONAL_STATUS_UP) {
                        if_index = static_cast<int32_t>(interface->ifIndex);
                        break;
                    }
                }
                ::nxIpStackFreeInfo(interfaces);
            }
        }
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IPV6, nxIPV6_MULTICAST_IF, &if_index, static_cast<nxsocklen_t>(sizeof(if_index)))
            == SOCKET_ERROR_VALUE) {
            ec = xnet_socket_helper::get_xnet_error();
            return;
        }
    } else {
        const auto protocol = boost::asio::ip::udp::v4();
        const auto option_size = outbound.size(protocol);
        if (option_size != sizeof(nxin_addr)) {
            ec = boost::asio::error::address_family_not_supported;
            return;
        }
        if (xnet_api::nxsetsockopt(socket_, nxIPPROTO_IP, nxIP_MULTICAST_IF, outbound.data(protocol), static_cast<nxsocklen_t>(option_size))
            == SOCKET_ERROR_VALUE) {
            ec = xnet_socket_helper::get_xnet_error();
            return;
        }
    }

    ec.clear();
}

#if defined(__linux__) || defined(__QNX__)
void xnet_udp_socket::set_option([[maybe_unused]] udp_bind_to_device _opt, boost::system::error_code& _ec) {
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    // SO_BINDTODEVICE is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
}

void xnet_udp_socket::set_option([[maybe_unused]] udp_packet_info_ip4 _opt, boost::system::error_code& _ec) {
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    // IP_PKTINFO is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
}

void xnet_udp_socket::set_option([[maybe_unused]] udp_packet_info_ip6 _opt, boost::system::error_code& _ec) {
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    // IPV6_RECVPKTINFO is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
}

void xnet_udp_socket::set_option([[maybe_unused]] udp_send_timeout _opt, boost::system::error_code& _ec) {
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    // SO_SNDTIMEO is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
}

void xnet_udp_socket::set_option([[maybe_unused]] udp_receive_timeout _opt, boost::system::error_code& _ec) {
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    // SO_RCVTIMEO is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
}

bool xnet_udp_socket::can_read_fd_flags() {
    return false;
}
#endif // defined(__linux__) || defined(__QNX__)

#ifdef __linux__
void xnet_udp_socket::set_option([[maybe_unused]] udp_receive_buffer_force _opt, boost::system::error_code& _ec) {
    if (!is_open()) { _ec = boost::asio::error::bad_descriptor; return; }
    // SO_RCVBUFFORCE is not supported by the XNET stack
    _ec = boost::asio::error::operation_not_supported;
}
#endif // __linux__

void xnet_udp_socket::get_option(boost::asio::ip::udp::socket::receive_buffer_size& rx_size, boost::system::error_code& ec) {
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return;
    }

    int value = 0;
    nxsocklen_t opt_len = static_cast<nxsocklen_t>(sizeof(value));
    if (xnet_api::nxgetsockopt(socket_, nxSOL_SOCKET, nxSO_RCVBUF, &value, &opt_len) == SOCKET_ERROR_VALUE || opt_len < static_cast<nxsocklen_t>(sizeof(value))) {
        ec = xnet_socket_helper::get_xnet_error();
        return;
    }

    rx_size = boost::asio::ip::udp::socket::receive_buffer_size(value);
    ec.clear();
}

boost::asio::ip::udp::endpoint xnet_udp_socket::local_endpoint(boost::system::error_code& ec) const {
    if (!is_open()) {
        ec = boost::asio::error::bad_descriptor;
        return {};
    }

    nxsockaddr_storage its_storage{};
    nxsocklen_t its_len = static_cast<nxsocklen_t>(sizeof(its_storage));

    if (xnet_api::nxgetsockname(socket_, reinterpret_cast<nxsockaddr*>(&its_storage), &its_len) == SOCKET_ERROR_VALUE) {
        ec = xnet_socket_helper::get_xnet_error();
        return {};
    }

    boost::asio::ip::address endpoint_address;
    boost::asio::ip::port_type endpoint_port;
    if (!xnet_socket_helper::native_to_endpoint(its_storage, its_len, endpoint_address, endpoint_port, ec)) {
        return {};
    }

    ec.clear();
    return boost::asio::ip::udp::endpoint(endpoint_address, endpoint_port);
}

void xnet_udp_socket::async_connect(boost::asio::ip::udp::endpoint const& remote, completion_handler handler) {
    if (!is_open()) {
        xnet_socket_helper::post_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* its_io = &io_context_;
    if (!general_worker_.enqueue([this, its_io, its_socket, its_epoch, remote, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;

        nxsockaddr_storage its_storage{};
        nxsocklen_t its_length = 0;
        if (!xnet_socket_helper::endpoint_to_native(remote.address(), remote.port(), its_storage, its_length, its_error)) {
            xnet_socket_helper::post_completion(*its_io, std::move(handler), its_error);
            return;
        }

        const auto its_result = xnet_api::nxconnect(its_socket, reinterpret_cast<nxsockaddr*>(&its_storage), its_length);

        if (its_result == SOCKET_ERROR_VALUE) {
            its_error = xnet_socket_helper::get_xnet_error();
            if (xnet_socket_helper::is_would_block_like(its_error)) {
                boost::system::error_code its_wait_error;
                if (xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, boost::asio::ip::udp::socket::wait_write,
                                                          cancel_epoch_, its_epoch, its_wait_error)) {
                    its_error.clear();
                } else {
                    its_error = its_wait_error;
                }
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
        }

        xnet_socket_helper::post_completion(*its_io, std::move(handler), its_error);
    })) {
        xnet_socket_helper::post_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted);
    }
}

void xnet_udp_socket::async_receive_from(boost::asio::mutable_buffer b, boost::asio::ip::udp::endpoint& remote, rw_handler handler) {
    if (!is_open()) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* const its_remote = &remote;
    auto its_receive_data = std::make_shared<std::vector<std::uint8_t>>(b.size());
    auto its_source_endpoint = std::make_shared<boost::asio::ip::udp::endpoint>();
    const auto its_buffer_size = static_cast<int32_t>(xnet_socket_helper::clamp_size_to_int32(its_receive_data->size()));

    auto* its_io = &io_context_;
    if (!receive_worker_.enqueue([this, its_io, its_socket, its_epoch, caller_buffer = b, its_remote, its_receive_data,
                               its_source_endpoint, its_buffer_size, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t its_bytes_received = 0;

        for (;;) {
            nxsockaddr_storage its_from_storage{};
            nxsocklen_t its_from_len = static_cast<nxsocklen_t>(sizeof(its_from_storage));
            auto* const its_receive_ptr = its_receive_data->empty() ? nullptr : its_receive_data->data();

            const auto its_result = xnet_api::nxrecvfrom(its_socket, its_receive_ptr, its_buffer_size, 0, reinterpret_cast<nxsockaddr*>(&its_from_storage), &its_from_len);

            if (its_result >= 0) {
                its_bytes_received = static_cast<std::size_t>(its_result);
                boost::asio::ip::address endpoint_address;
                boost::asio::ip::port_type endpoint_port;
                if (!xnet_socket_helper::native_to_endpoint(its_from_storage, its_from_len, endpoint_address, endpoint_port, its_error)) {
                    its_bytes_received = 0;
                }
                *its_source_endpoint = boost::asio::ip::udp::endpoint(endpoint_address, endpoint_port);
                break;
            }

            its_error = xnet_socket_helper::get_xnet_error();
            if (!xnet_socket_helper::is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, boost::asio::ip::udp::socket::wait_read, cancel_epoch_,
                                                       its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            its_bytes_received = 0;
        } else if (its_error == boost::asio::error::bad_descriptor) {
            // Socket teardown race on close/rejoin can surface as invalid descriptor
            // from nxrecvfrom/nxselect; treat it as a deterministic cancellation.
            its_error = boost::asio::error::operation_aborted;
            its_bytes_received = 0;
        }

        post_receive_from_completion(*its_io, std::move(handler), its_error, its_bytes_received,
                                     caller_buffer, its_remote, its_receive_data, its_source_endpoint);
    })) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_udp_socket::async_send(boost::asio::const_buffer const& b, rw_handler handler) {
    if (!is_open()) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto its_buffer_data = std::make_shared<std::vector<std::uint8_t>>();
    auto const* its_buffer_begin = static_cast<const std::uint8_t*>(b.data());
    if (b.size() > 0) {
        if (its_buffer_begin == nullptr) {
            xnet_socket_helper::post_rw_completion(io_context_, std::move(handler), boost::asio::error::invalid_argument, 0);
            return;
        }
        its_buffer_data->assign(its_buffer_begin, its_buffer_begin + b.size());
    }
    const auto its_buffer_size = static_cast<int32_t>(xnet_socket_helper::clamp_size_to_int32(its_buffer_data->size()));

    auto* its_io = &io_context_;
    if (!general_worker_.enqueue([this, its_io, its_socket, its_epoch, its_buffer_data, its_buffer_size, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t its_bytes_sent = 0;

        for (;;) {
            const auto its_result = xnet_api::nxsend(its_socket, its_buffer_data->data(), its_buffer_size, 0);

            if (its_result >= 0) {
                its_bytes_sent = static_cast<std::size_t>(its_result);
                its_error.clear();
                break;
            }

            its_error = xnet_socket_helper::get_xnet_error();
            if (!xnet_socket_helper::is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, boost::asio::ip::udp::socket::wait_write,
                                                       cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            its_bytes_sent = 0;
        }

        xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, its_bytes_sent);
    })) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_udp_socket::async_send_to(boost::asio::const_buffer const& b, boost::asio::ip::udp::endpoint destination, rw_handler handler) {
    if (!is_open()) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor, 0);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto its_buffer_data = std::make_shared<std::vector<std::uint8_t>>();
    auto const* its_buffer_begin = static_cast<const std::uint8_t*>(b.data());
    if (b.size() > 0) {
        if (its_buffer_begin == nullptr) {
            xnet_socket_helper::post_rw_completion(io_context_, std::move(handler), boost::asio::error::invalid_argument, 0);
            return;
        }
        its_buffer_data->assign(its_buffer_begin, its_buffer_begin + b.size());
    }
    const auto its_buffer_size = static_cast<int32_t>(xnet_socket_helper::clamp_size_to_int32(its_buffer_data->size()));

    auto* its_io = &io_context_;
    if (!general_worker_.enqueue([this, its_io, its_socket, its_epoch, destination, its_buffer_data, its_buffer_size, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;
        std::size_t its_bytes_sent = 0;

        nxsockaddr_storage its_destination_storage{};
        nxsocklen_t its_destination_length = 0;
        if (!xnet_socket_helper::endpoint_to_native(destination.address(), destination.port(), its_destination_storage, its_destination_length, its_error)) {
            xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, 0);
            return;
        }

        for (;;) {
            const auto its_result = xnet_api::nxsendto(its_socket, its_buffer_data->data(), its_buffer_size, 0,
                                             reinterpret_cast<nxsockaddr*>(&its_destination_storage), its_destination_length);

            if (its_result >= 0) {
                its_bytes_sent = static_cast<std::size_t>(its_result);
                its_error.clear();
                break;
            }

            its_error = xnet_socket_helper::get_xnet_error();
            if (!xnet_socket_helper::is_would_block_like(its_error)) {
                break;
            }

            boost::system::error_code its_wait_error;
            if (!xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, boost::asio::ip::udp::socket::wait_write,
                                                       cancel_epoch_, its_epoch, its_wait_error)) {
                its_error = its_wait_error;
                break;
            }
        }

        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
            its_bytes_sent = 0;
        }

        xnet_socket_helper::post_rw_completion(*its_io, std::move(handler), its_error, its_bytes_sent);
    })) {
        xnet_socket_helper::post_rw_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted, 0);
    }
}

void xnet_udp_socket::async_wait(boost::asio::ip::udp::socket::wait_type wait, completion_handler handler) {
    if (!is_open()) {
        xnet_socket_helper::post_completion(io_context_, std::move(handler), boost::asio::error::bad_descriptor);
        return;
    }

    const auto its_socket = socket_;
    const auto its_epoch = cancel_epoch_.load(std::memory_order_relaxed);
    auto* its_io = &io_context_;
    if (!general_worker_.enqueue([this, its_io, its_socket, its_epoch, wait, handler = std::move(handler)]() mutable {
        boost::system::error_code its_error;
        (void)xnet_socket_helper::wait_socket_ready(its_socket, std::nullopt, wait, cancel_epoch_, its_epoch, its_error);
        if (is_canceled(its_epoch)) {
            its_error = boost::asio::error::operation_aborted;
        }
        xnet_socket_helper::post_completion(*its_io, std::move(handler), its_error);
    })) {
        xnet_socket_helper::post_completion(io_context_, std::move(handler), boost::asio::error::operation_aborted);
    }

}

bool xnet_udp_socket::is_canceled(std::uint64_t _epoch) const {
    return cancel_epoch_.load(std::memory_order_relaxed) != _epoch;
}

void xnet_udp_socket::stop_worker_threads() {
    general_worker_.request_stop();
    receive_worker_.request_stop();

    general_worker_.join();
    receive_worker_.join();
}

}
