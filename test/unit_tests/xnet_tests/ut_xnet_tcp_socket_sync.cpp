#include <gtest/gtest.h>

#include <boost/asio/error.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/errc.hpp>

#include <cstring>
#include <memory>

#include "tcp_socket.hpp"
#include "xnet_api.hpp"
#include "xnet_tcp_socket.hpp"
#include "common/xnet_test_utils.hpp"

namespace {

struct fake_tcp_backend_state {
    nxSOCKET socket_to_return{static_cast<nxSOCKET>(4321)};
    int last_error{0};

    int socket_calls{0};
    int bind_calls{0};
    int close_calls{0};
    int setopt_calls{0};
    int getopt_calls{0};
    int getsockname_calls{0};

    int bind_result{0};
    int close_result{0};
    int setopt_result{0};

    bool fail_nonblock_setopt{false};
    bool fail_regular_setopt{false};

    int last_setsockopt_level{0};
    int last_setsockopt_name{0};

    int io_control_rx_data{73};

    boost::asio::ip::tcp::endpoint local_ep{boost::asio::ip::make_address_v4("127.0.0.10"), 31555};

    void reset() {
        *this = fake_tcp_backend_state{};
    }
};

fake_tcp_backend_state g_state;

auto fake_nxgetlasterrornum() {
    return static_cast<decltype(vsomeip_v3::xnet_api::nxgetlasterrornum())>(g_state.last_error);
}

nxSOCKET fake_nxsocket(nxIpStackRef_t, int, int, int) {
    ++g_state.socket_calls;
    return g_state.socket_to_return;
}

int32_t fake_nxbind(nxSOCKET, const nxsockaddr*, nxsocklen_t) {
    ++g_state.bind_calls;
    if (g_state.bind_result != 0) {
        g_state.last_error = 13850;
    }
    return g_state.bind_result;
}

int fake_nxclose(nxSOCKET) {
    ++g_state.close_calls;
    return g_state.close_result;
}

int fake_nxsetsockopt(nxSOCKET, int level, int name, const void*, nxsocklen_t) {
    ++g_state.setopt_calls;
    g_state.last_setsockopt_level = level;
    g_state.last_setsockopt_name = name;

    if (g_state.fail_nonblock_setopt && level == nxSOL_SOCKET && name == nxSO_NONBLOCK) {
        g_state.last_error = 13813;
        return -1;
    }

    if (g_state.fail_regular_setopt && !(level == nxSOL_SOCKET && name == nxSO_NONBLOCK)) {
        g_state.last_error = 13813;
        return -1;
    }

    return g_state.setopt_result;
}

int fake_nxgetsockopt(nxSOCKET, int, int name, void* value, nxsocklen_t* len) {
    ++g_state.getopt_calls;

    if (name == nxSO_RXDATA && value != nullptr && len != nullptr && *len >= static_cast<nxsocklen_t>(sizeof(int32_t))) {
        *reinterpret_cast<int32_t*>(value) = static_cast<int32_t>(g_state.io_control_rx_data);
        *len = static_cast<nxsocklen_t>(sizeof(int32_t));
        return 0;
    }

    g_state.last_error = 13813;
    return -1;
}

int fake_nxgetsockname(nxSOCKET, nxsockaddr* addr, nxsocklen_t* len) {
    ++g_state.getsockname_calls;

    if (addr == nullptr || len == nullptr || *len < static_cast<nxsocklen_t>(sizeof(nxsockaddr_in))) {
        g_state.last_error = 13813;
        return -1;
    }

    auto* out = reinterpret_cast<nxsockaddr_in*>(addr);
    std::memset(out, 0, sizeof(nxsockaddr_in));
    out->sin_family = nxAF_INET;
    out->sin_port = boost::endian::native_to_big(g_state.local_ep.port());
    out->sin_addr.addr = boost::endian::native_to_big(g_state.local_ep.address().to_v4().to_uint());
    *len = static_cast<nxsocklen_t>(sizeof(nxsockaddr_in));
    return 0;
}

struct xnet_tcp_socket_sync_fixture : ::testing::Test {
    std::unique_ptr<vsomeip_v3::xnet_api::api_table> original_table_;
    boost::asio::io_context io_;

    void SetUp() override {
        g_state.reset();
        original_table_ = std::make_unique<vsomeip_v3::xnet_api::api_table>(vsomeip_v3::xnet_api::get_api_table());

        auto table = *original_table_;
        table.nxgetlasterrornum_fn = &fake_nxgetlasterrornum;
        table.nxsocket_fn = &fake_nxsocket;
        table.nxbind_fn = &fake_nxbind;
        table.nxclose_fn = &fake_nxclose;
        table.nxsetsockopt_fn = &fake_nxsetsockopt;
        table.nxgetsockopt_fn = &fake_nxgetsockopt;
        table.nxgetsockname_fn = &fake_nxgetsockname;
        vsomeip_v3::xnet_api::set_api_table_for_test(table);
    }

    void TearDown() override {
        if (original_table_) {
            vsomeip_v3::xnet_api::set_api_table_for_test(*original_table_);
        }
    }

    std::unique_ptr<vsomeip_v3::tcp_socket> create_socket() {
        return std::make_unique<vsomeip_v3::xnet_tcp_socket>(io_, reinterpret_cast<nxIpStackRef_t>(0x1));
    }
};

}

TEST_F(xnet_tcp_socket_sync_fixture, open_sets_nonblocking_and_opens_socket) {
    auto socket = create_socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);

    xnet_test_utils::expect_success(ec);
    EXPECT_TRUE(socket->is_open());
    EXPECT_EQ(g_state.socket_calls, 1);
    EXPECT_GE(g_state.setopt_calls, 1);
    EXPECT_EQ(g_state.last_setsockopt_level, nxSOL_SOCKET);
    EXPECT_EQ(g_state.last_setsockopt_name, nxSO_NONBLOCK);

    socket->close(ec);
    xnet_test_utils::expect_success(ec);
}

TEST_F(xnet_tcp_socket_sync_fixture, open_fails_when_nonblocking_setup_fails_and_closes_socket) {
    auto socket = create_socket();
    g_state.fail_nonblock_setopt = true;

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);

    EXPECT_EQ(ec, boost::asio::error::make_error_code(boost::asio::error::access_denied));
    EXPECT_FALSE(socket->is_open());
    EXPECT_EQ(g_state.close_calls, 1);
}

TEST_F(xnet_tcp_socket_sync_fixture, bind_success_calls_backend_once) {
    auto socket = create_socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    socket->bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 32000), ec);

    xnet_test_utils::expect_success(ec);
    EXPECT_EQ(g_state.bind_calls, 1);
}

TEST_F(xnet_tcp_socket_sync_fixture, bind_backend_error_is_mapped) {
    auto socket = create_socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    g_state.bind_result = -1;
    socket->bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 32001), ec);

    EXPECT_EQ(ec, boost::system::errc::make_error_code(boost::system::errc::address_not_available));
    EXPECT_EQ(g_state.bind_calls, 1);
}

TEST_F(xnet_tcp_socket_sync_fixture, local_endpoint_and_io_control_reflect_backend_values) {
    auto socket = create_socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    auto local = socket->local_endpoint(ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_EQ(g_state.getsockname_calls, 1);
    EXPECT_EQ(local.address(), g_state.local_ep.address());
    EXPECT_EQ(local.port(), g_state.local_ep.port());

    vsomeip_v3::io_control_operation<std::size_t> pending_read(0);
    socket->io_control(pending_read, ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_EQ(g_state.getopt_calls, 1);
    EXPECT_EQ(pending_read.get(), static_cast<std::size_t>(g_state.io_control_rx_data));
}

TEST_F(xnet_tcp_socket_sync_fixture, set_option_keep_alive_returns_operation_not_supported_without_backend_call) {
    auto socket = create_socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    const auto setopt_calls_before = g_state.setopt_calls;
    socket->set_option(boost::asio::ip::tcp::socket::keep_alive(true), ec);

    EXPECT_EQ(ec, boost::asio::error::make_error_code(boost::asio::error::operation_not_supported));
    EXPECT_EQ(g_state.setopt_calls, setopt_calls_before);
}

TEST_F(xnet_tcp_socket_sync_fixture, set_option_no_delay_reuse_and_linger_call_backend) {
    auto socket = create_socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    socket->set_option(boost::asio::ip::tcp::no_delay(true), ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_EQ(g_state.last_setsockopt_level, nxIPPROTO_TCP);
    EXPECT_EQ(g_state.last_setsockopt_name, nxTCP_NODELAY);

    socket->set_option(boost::asio::ip::tcp::socket::reuse_address(true), ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_EQ(g_state.last_setsockopt_level, nxSOL_SOCKET);
    EXPECT_EQ(g_state.last_setsockopt_name, nxSO_REUSEADDR);

    socket->set_option(boost::asio::ip::tcp::socket::linger(true, 2), ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_EQ(g_state.last_setsockopt_level, nxSOL_SOCKET);
    EXPECT_EQ(g_state.last_setsockopt_name, nxSO_LINGER);
}

TEST_F(xnet_tcp_socket_sync_fixture, set_option_no_delay_failure_is_mapped) {
    auto socket = create_socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    g_state.fail_regular_setopt = true;
    socket->set_option(boost::asio::ip::tcp::no_delay(true), ec);

    EXPECT_EQ(ec, boost::asio::error::make_error_code(boost::asio::error::access_denied));
}

TEST_F(xnet_tcp_socket_sync_fixture, cancel_and_close_keep_lifecycle_deterministic) {
    auto socket = create_socket();

    boost::system::error_code ec;
    socket->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);
    ASSERT_TRUE(socket->is_open());

    socket->cancel(ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_TRUE(socket->is_open());

    socket->close(ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_FALSE(socket->is_open());
    EXPECT_EQ(g_state.close_calls, 1);
}