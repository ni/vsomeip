// XNET UDP Socket Wrapper for vsomeip

#ifndef XNET_UDP_SOCKET_HPP_
#define XNET_UDP_SOCKET_HPP_

#include "udp_socket.hpp"
#include <atomic>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/post.hpp>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "xnet_types.hpp"

namespace vsomeip_v3 {

class xnet_udp_socket final : public udp_socket {
public:
    // Constructor - store io_context reference for posting completions
    explicit xnet_udp_socket(boost::asio::io_context& _io, nxIpStackRef_t xnet_stack);
    xnet_udp_socket(boost::asio::io_context& _io, nxIpStackRef_t xnet_stack, std::shared_ptr<void> _stack_lifetime);
    ~xnet_udp_socket();

private:
    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] int native_handle() override;

    void open(boost::asio::ip::udp::endpoint::protocol_type pt, boost::system::error_code& ec) override;
    void bind(boost::asio::ip::udp::endpoint const& ep, boost::system::error_code& ec) override;

    void close(boost::system::error_code& ec) override;
    // Not part of udp_socket base interface; kept as helper API for xnet implementation only.
    void cancel(boost::system::error_code& ec);
    // Not part of udp_socket base interface; kept as helper API for xnet implementation only.
    void shutdown(boost::asio::ip::udp::socket::shutdown_type st, boost::system::error_code& ec);

    bool native_non_blocking() const override;
    void native_non_blocking(bool mode, boost::system::error_code& ec) override;

    void set_option(boost::asio::ip::udp::socket::reuse_address ra, boost::system::error_code& ec) override;
    void set_option(boost::asio::ip::multicast::outbound_interface outbound, boost::system::error_code& ec) override;
    void set_option(boost::asio::ip::udp::socket::broadcast broad, boost::system::error_code& ec) override;
    void set_option(boost::asio::ip::udp::socket::receive_buffer_size rx_size, boost::system::error_code& ec) override;
    void set_option(boost::asio::ip::multicast::join_group join, boost::system::error_code& ec) override;
    void set_option(boost::asio::ip::multicast::leave_group leave, boost::system::error_code& ec) override;
    void get_option(boost::asio::ip::udp::socket::receive_buffer_size& rx_size, boost::system::error_code& ec) override;

#if defined(__linux__) || defined(__QNX__)
    void set_option(udp_bind_to_device _opt, boost::system::error_code& _ec) override;
    void set_option(udp_packet_info_ip4 _opt, boost::system::error_code& _ec) override;
    void set_option(udp_packet_info_ip6 _opt, boost::system::error_code& _ec) override;
    void set_option(udp_send_timeout _opt, boost::system::error_code& _ec) override;
    void set_option(udp_receive_timeout _opt, boost::system::error_code& _ec) override;
    bool can_read_fd_flags() override;
#endif
#ifdef __linux__
    void set_option(udp_receive_buffer_force _opt, boost::system::error_code& _ec) override;
#endif

    boost::asio::ip::udp::endpoint local_endpoint(boost::system::error_code& ec) const override;

    void async_connect(boost::asio::ip::udp::endpoint const& remote, completion_handler handler) override;
    void async_receive_from(boost::asio::mutable_buffer b, boost::asio::ip::udp::endpoint& remote, rw_handler handler) override;
    void async_send(boost::asio::const_buffer const& b, rw_handler handler) override;
    void async_send_to(boost::asio::const_buffer const& b, boost::asio::ip::udp::endpoint destination, rw_handler handler) override;
    // Not part of udp_socket base interface; kept as helper API for xnet implementation only.
    void async_wait(boost::asio::ip::udp::socket::wait_type wait, completion_handler handler);

    // XNET socket handle
    nxSOCKET socket_;

    // Reference to io_context for posting completion handlers
    boost::asio::io_context& io_context_;

    // XNET IP stack reference
    nxIpStackRef_t xnet_stack_;
    std::shared_ptr<void> stack_lifetime_;

    // Track if socket is IPv6 for correct socket creation and option handling
    bool is_ipv6_;

    // Cached state for platforms/backends where querying non-blocking mode is limited.
    bool non_blocking_mode_;

    using work_item_t = std::function<void()>;

    void ensure_general_worker_thread();
    void ensure_receive_worker_thread();
    void stop_worker_threads();
    bool enqueue_work(work_item_t&& _item);
    bool enqueue_receive_work(work_item_t&& _item);
    void general_worker_loop();
    void receive_worker_loop();
    bool is_canceled(std::uint64_t _epoch) const;

    std::thread general_worker_thread_;
    std::thread receive_worker_thread_;
    std::atomic<bool> stop_requested_;
    std::atomic<std::uint64_t> cancel_epoch_;
    std::mutex general_worker_mutex_;
    std::condition_variable general_worker_cv_;
    std::deque<work_item_t> general_work_queue_;
    std::mutex receive_worker_mutex_;
    std::condition_variable receive_worker_cv_;
    std::deque<work_item_t> receive_work_queue_;
};

} 

#endif 
