#include <gtest/gtest.h>

#include <boost/asio/error.hpp>

#include "tcp_server_endpoint_impl.hpp"

namespace {

class fake_tcp_socket_for_policy final : public vsomeip_v3::tcp_socket {
public:
    boost::system::error_code no_delay_ec_;
    boost::system::error_code keep_alive_ec_;
    boost::system::error_code linger_ec_;

    int no_delay_calls_{0};
    int keep_alive_calls_{0};
    int linger_calls_{0};

    [[nodiscard]] bool is_open() const override { return true; }
    [[nodiscard]] int native_handle() override { return 0; }

    void open(boost::asio::ip::tcp::endpoint::protocol_type, boost::system::error_code& _ec) override { _ec.clear(); }
    void bind(boost::asio::ip::tcp::endpoint const&, boost::system::error_code& _ec) override { _ec.clear(); }

    void close(boost::system::error_code& _ec) override { _ec.clear(); }
    void cancel(boost::system::error_code& _ec) override { _ec.clear(); }

    boost::asio::ip::tcp::endpoint local_endpoint(boost::system::error_code& _ec) const override {
        _ec.clear();
        return {};
    }

    void io_control(vsomeip_v3::io_control_operation<std::size_t>&, boost::system::error_code& _ec) override { _ec.clear(); }

    void set_option(boost::asio::ip::tcp::no_delay, boost::system::error_code& _ec) override {
        ++no_delay_calls_;
        _ec = no_delay_ec_;
    }

    void set_option(boost::asio::ip::tcp::socket::keep_alive, boost::system::error_code& _ec) override {
        ++keep_alive_calls_;
        _ec = keep_alive_ec_;
    }

    void set_option(boost::asio::ip::tcp::socket::linger, boost::system::error_code& _ec) override {
        ++linger_calls_;
        _ec = linger_ec_;
    }

    void set_option(boost::asio::ip::tcp::socket::reuse_address, boost::system::error_code& _ec) override { _ec.clear(); }

    void async_connect(boost::asio::ip::tcp::endpoint const&, connect_handler _handler) override { _handler({}); }
    void async_receive(boost::asio::mutable_buffer, rw_handler _handler) override { _handler({}, 0); }
    void async_write(std::vector<boost::asio::const_buffer> const&, rw_handler _handler) override { _handler({}, 0); }
    void async_write(boost::asio::const_buffer const&, completion_condition, rw_handler _handler) override { _handler({}, 0); }
};

}

TEST(xnet_tcp_server_accept_policy_test, operation_not_supported_for_all_options_is_tolerated) {
    fake_tcp_socket_for_policy socket;
    const auto unsupported = boost::asio::error::make_error_code(boost::asio::error::operation_not_supported);

    socket.no_delay_ec_ = unsupported;
    socket.keep_alive_ec_ = unsupported;
    socket.linger_ec_ = unsupported;

    const auto result = apply_tcp_server_accept_socket_option_policy(socket, "[test]");

    EXPECT_FALSE(result);
    EXPECT_EQ(socket.no_delay_calls_, 1);
    EXPECT_EQ(socket.keep_alive_calls_, 1);
    EXPECT_EQ(socket.linger_calls_, 1);
}

TEST(xnet_tcp_server_accept_policy_test, unexpected_no_delay_error_remains_fatal) {
    fake_tcp_socket_for_policy socket;
    socket.no_delay_ec_ = boost::asio::error::make_error_code(boost::asio::error::access_denied);

    const auto result = apply_tcp_server_accept_socket_option_policy(socket, "[test]");

    EXPECT_EQ(result, boost::asio::error::make_error_code(boost::asio::error::access_denied));
    EXPECT_EQ(socket.no_delay_calls_, 1);
    EXPECT_EQ(socket.keep_alive_calls_, 1);
    EXPECT_EQ(socket.linger_calls_, 1);
}

TEST(xnet_tcp_server_accept_policy_test, unsupported_options_do_not_mask_fatal_keep_alive_error) {
    fake_tcp_socket_for_policy socket;
    const auto unsupported = boost::asio::error::make_error_code(boost::asio::error::operation_not_supported);

    socket.no_delay_ec_ = unsupported;
    socket.keep_alive_ec_ = boost::asio::error::make_error_code(boost::asio::error::connection_reset);
    socket.linger_ec_ = unsupported;

    const auto result = apply_tcp_server_accept_socket_option_policy(socket, "[test]");

    EXPECT_EQ(result, boost::asio::error::make_error_code(boost::asio::error::connection_reset));
    EXPECT_EQ(socket.no_delay_calls_, 1);
    EXPECT_EQ(socket.keep_alive_calls_, 1);
    EXPECT_EQ(socket.linger_calls_, 1);
}