// XNET TCP Socket Wrapper for vsomeip

#ifndef XNET_TCP_SOCKET_HPP_
#define XNET_TCP_SOCKET_HPP_

#include "tcp_socket.hpp"

#include <atomic>
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

class xnet_tcp_acceptor;

class xnet_tcp_socket final : public tcp_socket {
public:
    explicit xnet_tcp_socket(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack);
    xnet_tcp_socket(boost::asio::io_context& _io, nxIpStackRef_t _xnet_stack, std::shared_ptr<void> _stack_lifetime);
    ~xnet_tcp_socket() override;

private:
    friend class xnet_tcp_acceptor;

    void assign_accepted_socket(nxSOCKET _socket, bool _is_ipv6, boost::system::error_code& _ec);

    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] int native_handle() override;

    void open(boost::asio::ip::tcp::endpoint::protocol_type _pt, boost::system::error_code& _ec) override;
    void bind(boost::asio::ip::tcp::endpoint const& _ep, boost::system::error_code& _ec) override;

    void close(boost::system::error_code& _ec) override;
    void cancel(boost::system::error_code& _ec) override;

    boost::asio::ip::tcp::endpoint local_endpoint(boost::system::error_code& _ec) const override;

    void io_control(io_control_operation<std::size_t>& _icm, boost::system::error_code& _ec) override;

    void set_option(boost::asio::ip::tcp::no_delay _nd, boost::system::error_code& _ec) override;
    void set_option(boost::asio::ip::tcp::socket::keep_alive _ka, boost::system::error_code& _ec) override;
    void set_option(boost::asio::ip::tcp::socket::linger _l, boost::system::error_code& _ec) override;
    void set_option(boost::asio::ip::tcp::socket::reuse_address _ra, boost::system::error_code& _ec) override;

    void async_connect(boost::asio::ip::tcp::endpoint const& _ep, connect_handler _handler) override;
    void async_receive(boost::asio::mutable_buffer _b, rw_handler _handler) override;
    void async_write(std::vector<boost::asio::const_buffer> const& _bs, rw_handler _handler) override;
    void async_write(boost::asio::const_buffer const& _b, completion_condition _cc, rw_handler _handler) override;

#if defined(__linux__)
    [[nodiscard]] bool set_user_timeout(unsigned int _timeout) override;
    [[nodiscard]] bool set_keepidle(uint32_t _idle) override;
    [[nodiscard]] bool set_keepintvl(uint32_t _interval) override;
    [[nodiscard]] bool set_keepcnt(uint32_t _count) override;
    [[nodiscard]] bool set_quick_ack() override;
#endif

#if defined(__linux__) || defined(__QNX__)
    [[nodiscard]] bool bind_to_device(std::string const& _device) override;
    [[nodiscard]] bool can_read_fd_flags() override;
#endif

    [[nodiscard]] bool is_canceled(std::uint64_t _epoch) const;

    using work_item_t = std::function<void()>;

    void ensure_rx_worker_thread();
    void ensure_tx_worker_thread();
    void stop_worker_threads();
    bool enqueue_rx_work(work_item_t&& _item);
    bool enqueue_tx_work(work_item_t&& _item);
    void rx_worker_loop();
    void tx_worker_loop();

    nxSOCKET socket_;
    boost::asio::io_context& io_context_;
    nxIpStackRef_t xnet_stack_;
    std::shared_ptr<void> stack_lifetime_;
    bool is_ipv6_;
    bool non_blocking_mode_;
    std::thread rx_worker_thread_;
    std::thread tx_worker_thread_;
    std::atomic<bool> rx_stop_requested_;
    std::atomic<bool> tx_stop_requested_;
    std::atomic<std::uint64_t> cancel_epoch_;
    std::mutex rx_worker_mutex_;
    std::mutex tx_worker_mutex_;
    std::condition_variable rx_worker_cv_;
    std::condition_variable tx_worker_cv_;
    std::deque<work_item_t> rx_work_queue_;
    std::deque<work_item_t> tx_work_queue_;
};

}

#endif
