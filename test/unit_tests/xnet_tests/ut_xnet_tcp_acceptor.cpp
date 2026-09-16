#include <gtest/gtest.h>

#include <boost/asio/error.hpp>
#include <boost/endian/conversion.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

#include "tcp_socket.hpp"
#include "xnet_api.hpp"
#include "xnet_tcp_acceptor.hpp"
#include "xnet_tcp_socket.hpp"
#include "common/xnet_test_utils.hpp"

namespace {

struct fake_tcp_acceptor_backend_state {
    nxSOCKET acceptor_socket_to_return{static_cast<nxSOCKET>(6100)};
    nxSOCKET accepted_socket_to_return{static_cast<nxSOCKET>(7100)};

    int last_error{0};

    int socket_calls{0};
    int bind_calls{0};
    int listen_calls{0};
    int close_calls{0};
    int setopt_calls{0};
    int select_calls{0};
    int accept_calls{0};

    int bind_result{0};
    int listen_result{0};
    int close_result{0};
    int setopt_result{0};

    int select_result{1};
    int select_error{13813};
    int select_delay_ms{0};

    bool accept_fails{false};
    int accept_error{13837};

    int last_setsockopt_level{0};
    int last_setsockopt_name{0};

    boost::asio::ip::tcp::endpoint accepted_peer_ep{boost::asio::ip::make_address_v4("127.0.0.21"), 34567};

    void reset() { *this = fake_tcp_acceptor_backend_state{}; }
};

fake_tcp_acceptor_backend_state g_state;

auto fake_nxgetlasterrornum() {
    return static_cast<decltype(vsomeip_v3::xnet_api::nxgetlasterrornum())>(g_state.last_error);
}

nxSOCKET fake_nxsocket(nxIpStackRef_t, int, int, int) {
    ++g_state.socket_calls;
    return g_state.acceptor_socket_to_return;
}

int32_t fake_nxbind(nxSOCKET, const nxsockaddr*, nxsocklen_t) {
    ++g_state.bind_calls;
    if (g_state.bind_result != 0) {
        g_state.last_error = 13850;
    }
    return g_state.bind_result;
}

int fake_nxlisten(nxSOCKET, int) {
    ++g_state.listen_calls;
    if (g_state.listen_result != 0) {
        g_state.last_error = 13813;
    }
    return g_state.listen_result;
}

int fake_nxclose(nxSOCKET) {
    ++g_state.close_calls;
    return g_state.close_result;
}

int fake_nxsetsockopt(nxSOCKET, int level, int name, const void*, nxsocklen_t) {
    ++g_state.setopt_calls;
    g_state.last_setsockopt_level = level;
    g_state.last_setsockopt_name = name;

    if (g_state.setopt_result != 0) {
        g_state.last_error = 13813;
    }

    return g_state.setopt_result;
}

int fake_nxselect(int, nxfd_set*, nxfd_set*, nxfd_set*, nxtimeval*) {
    ++g_state.select_calls;

    if (g_state.select_delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_state.select_delay_ms));
    }

    if (g_state.select_result < 0) {
        g_state.last_error = g_state.select_error;
    }

    return g_state.select_result;
}

nxSOCKET fake_nxaccept(nxSOCKET, nxsockaddr* addr, nxsocklen_t* len) {
    ++g_state.accept_calls;

    if (g_state.accept_fails) {
        g_state.last_error = g_state.accept_error;
        return nxINVALID_SOCKET;
    }

    if (addr != nullptr && len != nullptr && *len >= static_cast<nxsocklen_t>(sizeof(nxsockaddr_in))) {
        auto* out = reinterpret_cast<nxsockaddr_in*>(addr);
        std::memset(out, 0, sizeof(nxsockaddr_in));
        out->sin_family = nxAF_INET;
        out->sin_port = boost::endian::native_to_big(g_state.accepted_peer_ep.port());
        out->sin_addr.addr = boost::endian::native_to_big(g_state.accepted_peer_ep.address().to_v4().to_uint());
        *len = static_cast<nxsocklen_t>(sizeof(nxsockaddr_in));
    }

    return g_state.accepted_socket_to_return;
}

struct xnet_tcp_acceptor_fixture : ::testing::Test {
    std::unique_ptr<vsomeip_v3::xnet_api::api_table> original_table_;
    boost::asio::io_context io_;

    void SetUp() override {
        g_state.reset();
        original_table_ = std::make_unique<vsomeip_v3::xnet_api::api_table>(vsomeip_v3::xnet_api::get_api_table());

        auto table = *original_table_;
        table.nxgetlasterrornum_fn = &fake_nxgetlasterrornum;
        table.nxsocket_fn = &fake_nxsocket;
        table.nxbind_fn = &fake_nxbind;
        table.nxlisten_fn = &fake_nxlisten;
        table.nxclose_fn = &fake_nxclose;
        table.nxsetsockopt_fn = &fake_nxsetsockopt;
        table.nxselect_fn = &fake_nxselect;
        table.nxaccept_fn = &fake_nxaccept;
        vsomeip_v3::xnet_api::set_api_table_for_test(table);
    }

    void TearDown() override {
        if (original_table_) {
            vsomeip_v3::xnet_api::set_api_table_for_test(*original_table_);
        }
    }

    std::unique_ptr<vsomeip_v3::tcp_acceptor> create_acceptor() {
        return std::make_unique<vsomeip_v3::xnet_tcp_acceptor>(io_, reinterpret_cast<nxIpStackRef_t>(0x1));
    }

    std::unique_ptr<vsomeip_v3::tcp_socket> create_peer_socket() {
        return std::make_unique<vsomeip_v3::xnet_tcp_socket>(io_, reinterpret_cast<nxIpStackRef_t>(0x1));
    }

    void pump_until(xnet_test_utils::async_completion_probe& probe, std::chrono::milliseconds timeout) {
        auto const end = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < end) {
            io_.restart();
            (void)io_.poll();
            if (probe.signal_count() >= 1U) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
};
}

TEST_F(xnet_tcp_acceptor_fixture, open_bind_listen_set_option_and_close_success) {
    auto acceptor = create_acceptor();

    boost::system::error_code ec;
    acceptor->open(boost::asio::ip::tcp::v4(), ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_TRUE(acceptor->is_open());

    acceptor->set_option(boost::asio::ip::tcp::socket::reuse_address(true), ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_EQ(g_state.last_setsockopt_level, nxSOL_SOCKET);
    EXPECT_EQ(g_state.last_setsockopt_name, nxSO_REUSEADDR);

    acceptor->bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 35000), ec);
    xnet_test_utils::expect_success(ec);

    acceptor->listen(8, ec);
    xnet_test_utils::expect_success(ec);

    EXPECT_EQ(g_state.socket_calls, 1);
    EXPECT_EQ(g_state.bind_calls, 1);
    EXPECT_EQ(g_state.listen_calls, 1);

    acceptor->close(ec);
    xnet_test_utils::expect_success(ec);
    EXPECT_FALSE(acceptor->is_open());
    EXPECT_EQ(g_state.close_calls, 1);
}

TEST_F(xnet_tcp_acceptor_fixture, wait_for_pending_connection_ready_returns_true) {
    auto acceptor = create_acceptor();

    boost::system::error_code ec;
    acceptor->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    g_state.select_result = 1;

    const auto pending = acceptor->wait_for_pending_connection(std::chrono::milliseconds(30), ec);

    EXPECT_TRUE(pending);
    xnet_test_utils::expect_success(ec);
    EXPECT_EQ(g_state.select_calls, 1);
}

TEST_F(xnet_tcp_acceptor_fixture, wait_for_pending_connection_timeout_returns_false_without_error) {
    auto acceptor = create_acceptor();

    boost::system::error_code ec;
    acceptor->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    g_state.select_result = 0;

    const auto pending = acceptor->wait_for_pending_connection(std::chrono::milliseconds(20), ec);

    EXPECT_FALSE(pending);
    xnet_test_utils::expect_success(ec);
}

TEST_F(xnet_tcp_acceptor_fixture, wait_for_pending_connection_error_is_mapped) {
    auto acceptor = create_acceptor();

    boost::system::error_code ec;
    acceptor->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    g_state.select_result = -1;
    g_state.select_error = 13813;

    const auto pending = acceptor->wait_for_pending_connection(std::chrono::milliseconds(20), ec);

    EXPECT_FALSE(pending);
    EXPECT_EQ(ec, boost::asio::error::make_error_code(boost::asio::error::access_denied));
}

TEST_F(xnet_tcp_acceptor_fixture, wait_for_pending_connection_cancel_returns_operation_aborted) {
    auto acceptor = create_acceptor();

    boost::system::error_code ec;
    acceptor->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    g_state.select_result = 0;
    g_state.select_delay_ms = 20;

    std::thread canceller([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        boost::system::error_code cancel_ec;
        acceptor->cancel(cancel_ec);
    });

    const auto pending = acceptor->wait_for_pending_connection(std::chrono::milliseconds(50), ec);

    canceller.join();

    EXPECT_FALSE(pending);
    EXPECT_EQ(ec, boost::asio::error::make_error_code(boost::asio::error::operation_aborted));
}

TEST_F(xnet_tcp_acceptor_fixture, async_accept_success_sets_peer_endpoint_and_opens_peer_socket) {
    auto acceptor = create_acceptor();
    auto peer_socket = create_peer_socket();

    boost::system::error_code ec;
    acceptor->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    g_state.select_result = 1;
    g_state.accept_fails = false;

    boost::asio::ip::tcp::endpoint peer_ep;
    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;

    acceptor->async_accept(*peer_socket, peer_ep, [&](boost::system::error_code const& _ec) {
        ++handler_calls;
        completion_ec = _ec;
        probe.signal();
    });

    pump_until(probe, std::chrono::milliseconds(350));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_EQ(g_state.accept_calls, 1);
    EXPECT_TRUE(peer_socket->is_open());
    EXPECT_EQ(peer_ep.address(), g_state.accepted_peer_ep.address());
    EXPECT_EQ(peer_ep.port(), g_state.accepted_peer_ep.port());

    peer_socket->close(ec);
    xnet_test_utils::expect_success(ec);
}

TEST_F(xnet_tcp_acceptor_fixture, async_accept_cancel_race_reports_operation_aborted) {
    auto acceptor = create_acceptor();
    auto peer_socket = create_peer_socket();

    boost::system::error_code ec;
    acceptor->open(boost::asio::ip::tcp::v4(), ec);
    ASSERT_FALSE(ec);

    g_state.select_result = 0;
    g_state.select_delay_ms = 20;

    boost::asio::ip::tcp::endpoint peer_ep;
    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;

    acceptor->async_accept(*peer_socket, peer_ep, [&](boost::system::error_code const& _ec) {
        ++handler_calls;
        completion_ec = _ec;
        probe.signal();
    });

    const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
    while (g_state.select_calls == 0 && std::chrono::steady_clock::now() < cancel_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    boost::system::error_code cancel_ec;
    acceptor->cancel(cancel_ec);
    xnet_test_utils::expect_success(cancel_ec);

    pump_until(probe, std::chrono::milliseconds(450));

    EXPECT_EQ(handler_calls.load(), 1);
    EXPECT_EQ(completion_ec, boost::asio::error::make_error_code(boost::asio::error::operation_aborted));
}

TEST_F(xnet_tcp_acceptor_fixture, set_option_requires_open_socket) {
    auto acceptor = create_acceptor();

    boost::system::error_code ec;
    acceptor->set_option(boost::asio::ip::tcp::socket::reuse_address(true), ec);

    EXPECT_EQ(ec, boost::asio::error::make_error_code(boost::asio::error::bad_descriptor));
}