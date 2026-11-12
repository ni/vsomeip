#pragma once

#include <mutex>
#include <utility>
#include <vector>

#include "asio_socket_factory.hpp"
#include "xnet_socket_factory.hpp"

// Socket factory used by the application-level XNET tests.
//
// - Reports itself as the XNET backend and keeps the IP stack registry of the real
//   xnet_socket_factory, so application_impl and endpoint_manager_impl take the XNET paths.
// - Creates ASIO objects, so no real XNET hardware/stack is touched.
// - Records every registration call so tests can verify who registered what.
class recording_socket_factory final : public vsomeip_v3::abstract_socket_factory {
public:
    struct stack_registration {
        const boost::asio::io_context* io;
        nxIpStackRef_t stack;
    };

    struct auxiliary_registration {
        const boost::asio::io_context* io;
        const boost::asio::io_context* auxiliary_io;
    };

#if defined(__linux__)
    std::shared_ptr<vsomeip_v3::abstract_netlink_connector> create_netlink_connector(boost::asio::io_context& _io,
                                                                                     const boost::asio::ip::address& _address,
                                                                                     const boost::asio::ip::address& _multicast_address,
                                                                                     bool _is_requiring_link) override {
        return xnet_.create_netlink_connector(_io, _address, _multicast_address, _is_requiring_link);
    }
#endif

    std::unique_ptr<vsomeip_v3::tcp_socket> create_tcp_socket(boost::asio::io_context& _io) override {
        return asio_.create_tcp_socket(_io);
    }
    std::unique_ptr<vsomeip_v3::tcp_acceptor> create_tcp_acceptor(boost::asio::io_context& _io) override {
        return asio_.create_tcp_acceptor(_io);
    }
    std::unique_ptr<vsomeip_v3::udp_socket> create_udp_socket(boost::asio::io_context& _io) override {
        return asio_.create_udp_socket(_io);
    }

#if defined(__linux__) || defined(__QNX__)
    std::unique_ptr<vsomeip_v3::uds_socket> create_uds_socket(boost::asio::io_context& _io) override {
        return asio_.create_uds_socket(_io);
    }
    std::unique_ptr<vsomeip_v3::uds_acceptor> create_uds_acceptor(boost::asio::io_context& _io) override {
        return asio_.create_uds_acceptor(_io);
    }
#endif

    std::unique_ptr<vsomeip_v3::abstract_timer> create_timer(boost::asio::io_context& _io) override { return asio_.create_timer(_io); }

    std::shared_ptr<vsomeip_v3::abstract_clock> get_clock() override { return asio_.get_clock(); }

    void register_ip_stack(boost::asio::io_context& _io, nxIpStackRef_t _stack) override {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            stack_registrations_.push_back({&_io, _stack});
        }
        xnet_.register_ip_stack(_io, _stack);
    }

    void register_ip_stack(boost::asio::io_context& _io, boost::asio::io_context& _auxiliary_io) override {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            auxiliary_registrations_.push_back({&_io, &_auxiliary_io});
        }
        xnet_.register_ip_stack(_io, _auxiliary_io);
    }

    void unregister_ip_stack(boost::asio::io_context& _io) override {
        {
            std::lock_guard<std::mutex> lock{mutex_};
            unregistrations_.push_back(&_io);
        }
        xnet_.unregister_ip_stack(_io);
    }

    bool is_xnet_backend() const override { return true; }

    nxIpStackRef_t get_xnet_ip_stack(boost::asio::io_context& _io) const override { return xnet_.get_xnet_ip_stack(_io); }

    nxIpStackRef_t get_xnet_ip_stack(const boost::asio::io_context* _io) const {
        return xnet_.get_xnet_ip_stack(*const_cast<boost::asio::io_context*>(_io));
    }

    std::vector<stack_registration> stack_registrations() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return stack_registrations_;
    }

    std::vector<auxiliary_registration> auxiliary_registrations() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return auxiliary_registrations_;
    }

    std::vector<const boost::asio::io_context*> unregistrations() const {
        std::lock_guard<std::mutex> lock{mutex_};
        return unregistrations_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock{mutex_};
        stack_registrations_.clear();
        auxiliary_registrations_.clear();
        unregistrations_.clear();
    }

private:
    vsomeip_v3::asio_socket_factory asio_;
    vsomeip_v3::xnet_socket_factory xnet_;

    mutable std::mutex mutex_;
    std::vector<stack_registration> stack_registrations_;
    std::vector<auxiliary_registration> auxiliary_registrations_;
    std::vector<const boost::asio::io_context*> unregistrations_;
};

// Process-wide instance, installed in main() before any vsomeip application is created.
recording_socket_factory& get_recording_socket_factory();
