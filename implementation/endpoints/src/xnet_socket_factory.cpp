// XNET Socket Factory Implementation

#include "../include/xnet_socket_factory.hpp"
#if defined(VSOMEIP_ENABLE_XNET)
#include "../include/xnet_tcp_acceptor.hpp"
#include "../include/xnet_tcp_socket.hpp"
#include "../include/xnet_udp_socket.hpp"
#endif

// Standard ASIO socket implementations (fallback when XNET disabled)
#include "../include/asio_udp_socket.hpp"
#include "../include/asio_tcp_socket.hpp"  // Contains both asio_tcp_socket and asio_tcp_acceptor
#include "../include/asio_timer.hpp"

#include <iostream>   
#include <stdexcept>  
#include <string>

#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX "xsf"

#if defined(__linux__)
#include "../include/netlink_connector.hpp"

namespace {

// When XNET is active, the IP stack is managed by the XNET hardware, not the OS kernel.
// The standard netlink_connector watches the OS kernel for the configured unicast address,
// which will never appear there. This bypass immediately reports the interface and route as
// available so vsomeip's routing manager does not wait forever for netlink events that
// will never come.
class xnet_netlink_bypass final : public vsomeip_v3::abstract_netlink_connector {
public:
    void register_net_if_changes_handler(const vsomeip_v3::net_if_changed_handler_t& _handler) override {
        handler_ = _handler;
    }
    void unregister_net_if_changes_handler() override {
        handler_ = nullptr;
    }
    void start() override {
        if (handler_) {
            VSOMEIP_INFO << "[XNET][netlink_bypass] Reporting interface UP (XNET manages its own IP stack)";
            handler_(true,  "xnet", true);   // interface available
            handler_(false, "xnet", true);   // multicast route available
        }
    }
    void stop() override {}
private:
    vsomeip_v3::net_if_changed_handler_t handler_;
};

} // namespace
#endif

#if defined(__linux__) || defined(__QNX__)
#include "../include/asio_uds_socket.hpp"
#include "../include/asio_uds_acceptor.hpp"
#endif

namespace vsomeip_v3 {

namespace {

constexpr char const* k_xnet_backend_tag = "backend=xnet";

void log_startup_report(nxIpStackRef_t _stack) {
    VSOMEIP_INFO << "[XNET][factory][startup] "
                 << k_xnet_backend_tag
                 << " stack_ref=" << _stack
                 << " stack_ready=" << (_stack != nullptr ? "true" : "false")
                 << " compile_gate=VSOMEIP_ENABLE_XNET";
}

} // namespace

#if defined(VSOMEIP_ENABLE_XNET)
// Defined only when XNET is enabled because the nxIpStackRef_t-based constructor
// is conditionally declared in the header under VSOMEIP_ENABLE_XNET.
xnet_socket_factory::xnet_socket_factory(nxIpStackRef_t xnet_stack) 
    : xnet_stack_(xnet_stack) {
        log_startup_report(xnet_stack_);
}
#endif

// Always provide the default constructor because it is always declared in the header.
xnet_socket_factory::xnet_socket_factory() {
    log_startup_report(xnet_stack_);
}

#if defined(__linux__)
    std::shared_ptr<abstract_netlink_connector>
        xnet_socket_factory::create_netlink_connector(boost::asio::io_context& _io, const boost::asio::ip::address& _address,
            const boost::asio::ip::address& _multicast_address, bool _is_requiring_link) {
#if defined(VSOMEIP_ENABLE_XNET)
        // XNET manages its own IP stack — the OS kernel never gets the configured unicast
        // address. Skip the real netlink watcher and immediately signal interface+route ready.
        (void)_io; (void)_address; (void)_multicast_address; (void)_is_requiring_link;
        return std::make_shared<xnet_netlink_bypass>();
#else
        return std::make_shared<netlink_connector>(_io, _address, _multicast_address, _is_requiring_link);
#endif
    }
#endif

    std::unique_ptr<udp_socket> xnet_socket_factory::create_udp_socket(boost::asio::io_context& _io) {
        #if defined(VSOMEIP_ENABLE_XNET)
            if (!is_xnet_enabled()) {
                VSOMEIP_ERROR << "[XNET][factory][create_udp_socket] " << k_xnet_backend_tag
                              << " failure_class=stack_init stack_ref=null";
                throw std::runtime_error("xnet_socket_factory: XNET backend selected but stack is null (udp_socket)");
            }
            VSOMEIP_INFO << "[XNET][factory][create_udp_socket] " << k_xnet_backend_tag
                         << " stack_ref=" << xnet_stack_;
            return std::make_unique<xnet_udp_socket>(_io, xnet_stack_);
        #else
            return std::make_unique<asio_udp_socket>(_io);
        #endif
    }

    std::unique_ptr<tcp_socket> xnet_socket_factory::create_tcp_socket(boost::asio::io_context& _io) {
        #if defined(VSOMEIP_ENABLE_XNET)
            if (!is_xnet_enabled()) {
                VSOMEIP_ERROR << "[XNET][factory][create_tcp_socket] " << k_xnet_backend_tag
                              << " failure_class=stack_init stack_ref=null";
                throw std::runtime_error("xnet_socket_factory: XNET backend selected but stack is null (tcp_socket)");
            }
            VSOMEIP_INFO << "[XNET][factory][create_tcp_socket] " << k_xnet_backend_tag
                         << " stack_ref=" << xnet_stack_;
            return std::make_unique<xnet_tcp_socket>(_io, xnet_stack_);
        #else
            return std::make_unique<asio_tcp_socket>(_io);
        #endif
    }

    std::unique_ptr<tcp_acceptor> xnet_socket_factory::create_tcp_acceptor(boost::asio::io_context& _io) {
        #if defined(VSOMEIP_ENABLE_XNET)
            if (!is_xnet_enabled()) {
                VSOMEIP_ERROR << "[XNET][factory][create_tcp_acceptor] " << k_xnet_backend_tag
                              << " failure_class=stack_init stack_ref=null";
                throw std::runtime_error("xnet_socket_factory: XNET backend selected but stack is null (tcp_acceptor)");
            }
            VSOMEIP_INFO << "[XNET][factory][create_tcp_acceptor] " << k_xnet_backend_tag
                         << " stack_ref=" << xnet_stack_;
            return std::make_unique<xnet_tcp_acceptor>(_io, xnet_stack_);
        #else
            return std::make_unique<asio_tcp_acceptor>(_io);
        #endif
    }

    std::unique_ptr<abstract_timer> xnet_socket_factory::create_timer(boost::asio::io_context& _io) {
        // Timers always use ASIO
        return std::make_unique<asio_timer>(_io);
    }

    bool xnet_socket_factory::is_xnet_backend() const {
        return is_xnet_enabled();
    }

#if defined(__linux__) || defined(__QNX__)
    std::unique_ptr<uds_socket> xnet_socket_factory::create_uds_socket(boost::asio::io_context& _io) {
        // Unix Domain Sockets ALWAYS use ASIO (local IPC only)
        return std::make_unique<asio_uds_socket>(_io);
    }

    std::unique_ptr<uds_acceptor> xnet_socket_factory::create_uds_acceptor(boost::asio::io_context& _io) {
        // Unix Domain Socket acceptor ALWAYS uses ASIO (local IPC only)
        return std::make_unique<asio_uds_acceptor>(_io);
    }
#endif

}
