#include "../include/xnet_socket_factory.hpp"
#include <vsomeip/xnet.hpp>

#include "../include/xnet_tcp_acceptor.hpp"
#include "../include/xnet_tcp_socket.hpp"
#include "../include/xnet_udp_socket.hpp"
#if defined(__linux__) || defined(__QNX__)
#include "../include/asio_uds_socket.hpp"
#include "../include/asio_uds_acceptor.hpp"
#endif
#include "../include/asio_timer.hpp"
#include "../include/steady_clock.hpp"
#include "logger_ext.hpp"

#define VSOMEIP_LOG_PREFIX "[XNET][factory]"

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
            VSOMEIP_INFO_P << "netlink_bypass: Reporting interface UP (XNET manages its own IP stack)";
            handler_(true,  "xnet", true);   // interface available
            handler_(false, "xnet", true);   // multicast route available
        }
    }
    void stop() override {}
private:
    vsomeip_v3::net_if_changed_handler_t handler_;
};

}
#endif

namespace vsomeip_v3 {

#if defined(__linux__)
    std::shared_ptr<abstract_netlink_connector>
        xnet_socket_factory::create_netlink_connector(boost::asio::io_context& _io, const boost::asio::ip::address& _address,
            const boost::asio::ip::address& _multicast_address, bool _is_requiring_link) {
        // XNET manages its own IP stack — the OS kernel never gets the configured unicast
        // address. Skip the real netlink watcher and immediately signal interface+route ready.
        (void)_io; (void)_address; (void)_multicast_address; (void)_is_requiring_link;
        return std::make_shared<xnet_netlink_bypass>();
    }
#endif

    std::unique_ptr<udp_socket> xnet_socket_factory::create_udp_socket(boost::asio::io_context& _io) {
        return std::make_unique<xnet_udp_socket>(_io, get_xnet_ip_stack(_io));
    }

    std::unique_ptr<tcp_socket> xnet_socket_factory::create_tcp_socket(boost::asio::io_context& _io) {
        return std::make_unique<xnet_tcp_socket>(_io, get_xnet_ip_stack(_io));
    }

    std::unique_ptr<tcp_acceptor> xnet_socket_factory::create_tcp_acceptor(boost::asio::io_context& _io) {
        return std::make_unique<xnet_tcp_acceptor>(_io, get_xnet_ip_stack(_io));
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

    std::unique_ptr<abstract_timer> xnet_socket_factory::create_timer(boost::asio::io_context& _io) {
        // Timers always use ASIO
        return std::make_unique<asio_timer>(_io);
    }

    std::shared_ptr<abstract_clock> xnet_socket_factory::get_clock() {
        static auto clock = std::make_shared<steady_clock>();
        return clock;
    }

    void xnet_socket_factory::register_ip_stack(boost::asio::io_context& _io, nxIpStackRef_t _stack) {
        std::lock_guard<std::mutex> lock{stacks_mutex_};
        stacks_[&_io] = _stack;
    }

    void xnet_socket_factory::register_ip_stack(boost::asio::io_context& _io, boost::asio::io_context& _auxiliary_io) {
        std::lock_guard<std::mutex> lock{stacks_mutex_};
        auto stack = stacks_.find(&_io);
        if (stack == stacks_.end()) {
            VSOMEIP_ERROR_P << "No IP stack registered for io_context: " << &_io;
            return;
        }
        stacks_[&_auxiliary_io] = stack->second;
    }

    void xnet_socket_factory::unregister_ip_stack(boost::asio::io_context& _io) {
        std::lock_guard<std::mutex> lock{stacks_mutex_};
        stacks_.erase(&_io);
    }

    nxIpStackRef_t xnet_socket_factory::get_xnet_ip_stack(boost::asio::io_context& _io) const {
        std::lock_guard<std::mutex> lock{stacks_mutex_};
        auto stack = stacks_.find(&_io);
        if (stack != stacks_.end()) {
            return stack->second;
        }
        VSOMEIP_ERROR_P << "No IP stack registered for io_context: " << &_io;
        return nullptr;
    }

bool use_xnet_socket_factory() {
    // call set_abstract_factory() only once and check if the factory was already initialized (ASIO) 
    static const bool is_xnet_active = [] {
        set_abstract_factory(std::make_shared<xnet_socket_factory>());
        return abstract_socket_factory::get()->is_xnet_backend();
    }();

    if (!is_xnet_active) {
        VSOMEIP_ERROR_P << "Late xnet_socket_factory injection: the socket factory was already initialized (ASIO).";
    }
    return is_xnet_active;
}

}
