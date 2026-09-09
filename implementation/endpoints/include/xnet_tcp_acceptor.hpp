// XNET TCP Acceptor Wrapper for vsomeip

#ifndef XNET_TCP_ACCEPTOR_HPP_
#define XNET_TCP_ACCEPTOR_HPP_

#include "tcp_socket.hpp"

#include <atomic>
#include <chrono>
#include <boost/asio/io_context.hpp>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "xnet_types.hpp"

namespace vsomeip_v3 {

class xnet_tcp_acceptor final : public tcp_acceptor {
public:
    explicit xnet_tcp_acceptor(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack);
    xnet_tcp_acceptor(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack, std::shared_ptr<void> _stack_lifetime);
    ~xnet_tcp_acceptor() override;

private:
    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] int native_handle() override;

    void open(boost::asio::ip::tcp::endpoint::protocol_type _pt, boost::system::error_code& _ec) override;
    void bind(boost::asio::ip::tcp::endpoint const& _ep, boost::system::error_code& _ec) override;
    void close(boost::system::error_code& _ec) override;
    void cancel(boost::system::error_code& _ec) override;
    void listen(int _backlog, boost::system::error_code& _ec) override;
    [[nodiscard]] bool wait_for_pending_connection(std::chrono::milliseconds _timeout, boost::system::error_code& _ec) override;

    void set_option(boost::asio::ip::tcp::socket::reuse_address _ra, boost::system::error_code& _ec) override;

#if defined(__linux__)
    [[nodiscard]] bool set_reuse_port() override;
    [[nodiscard]] bool set_native_option_free_bind() override;
#endif

#if defined(__linux__) || defined(__QNX__)
    [[nodiscard]] bool bind_to_device(std::string const& _device) override;
#endif

    void async_accept(tcp_socket& _socket, boost::asio::ip::tcp::endpoint& _peer_ep, connect_handler _handler) override;

    [[nodiscard]] bool is_canceled(std::uint64_t _epoch) const;

    using work_item_t = std::function<void()>;

    void ensure_worker_thread();
    void stop_worker_thread();
    bool enqueue_work(work_item_t&& _item);
    void worker_loop();

    nxSOCKET acceptor_;
    boost::asio::io_context& io_context_;
    nxIpStackRef_t xnet_stack_;
    std::shared_ptr<void> stack_lifetime_;
    bool is_ipv6_;
    std::thread worker_thread_;
    std::atomic<bool> stop_requested_;
    std::atomic<std::uint64_t> cancel_epoch_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    std::deque<work_item_t> work_queue_;
};

} // namespace vsomeip_v3

#endif
