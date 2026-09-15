#include <gtest/gtest.h>

#include <boost/asio/error.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "tcp_socket.hpp"
#include "xnet_api.hpp"
#include "xnet_tcp_socket.hpp"
#include "common/xnet_test_utils.hpp"

namespace {

struct fake_tcp_async_backend_state {
    nxSOCKET socket_to_return{static_cast<nxSOCKET>(5544)};

    int last_error{0};
    int connect_error{13837};
    int recv_error{13837};
    int send_error{13837};

    int connect_result{0};
    int connect_calls{0};

    int select_result{1};
    int select_calls{0};

    int so_error_value{0};
    int so_error_queries{0};

    int recv_calls{0};
    std::vector<int32_t> recv_results{};
    std::size_t recv_result_index{0};
    std::vector<std::uint8_t> recv_payload{};

    int send_calls{0};
    std::vector<int32_t> send_results{};
    std::size_t send_result_index{0};

    void reset() { *this = fake_tcp_async_backend_state{}; }
};

fake_tcp_async_backend_state g_state;

auto fake_nxgetlasterrornum() {
    return static_cast<decltype(vsomeip_v3::xnet_api::nxgetlasterrornum())>(g_state.last_error);
}

nxSOCKET fake_nxsocket(nxIpStackRef_t, int, int, int) {
    return g_state.socket_to_return;
}

int fake_nxsetsockopt(nxSOCKET, int, int, const void*, nxsocklen_t) {
    return 0;
}

int fake_nxclose(nxSOCKET) {
    return 0;
}

int32_t fake_nxconnect(nxSOCKET, const nxsockaddr*, nxsocklen_t) {
    ++g_state.connect_calls;
    if (g_state.connect_result == -1) {
        g_state.last_error = g_state.connect_error;
    }
    return g_state.connect_result;
}

int fake_nxgetsockopt(nxSOCKET, int, int name, void* value, nxsocklen_t* len) {
    if (name == nxSO_ERROR && value != nullptr && len != nullptr && *len >= static_cast<nxsocklen_t>(sizeof(int32_t))) {
        ++g_state.so_error_queries;
        *reinterpret_cast<int32_t*>(value) = static_cast<int32_t>(g_state.so_error_value);
        *len = static_cast<nxsocklen_t>(sizeof(int32_t));
        return 0;
    }

    g_state.last_error = 13813;
    return -1;
}

int32_t fake_nxrecv(nxSOCKET, void* buffer, int32_t len, int) {
    ++g_state.recv_calls;

    int32_t result = 0;
    if (g_state.recv_result_index < g_state.recv_results.size()) {
        result = g_state.recv_results[g_state.recv_result_index++];
    }

    if (result > 0 && buffer != nullptr) {
        const auto copy_len = static_cast<std::size_t>(std::min<int32_t>(result, len));
        if (!g_state.recv_payload.empty() && copy_len > 0) {
            std::memcpy(buffer, g_state.recv_payload.data(), std::min(copy_len, g_state.recv_payload.size()));
        }
        return result;
    }

    if (result < 0) {
        g_state.last_error = g_state.recv_error;
    }

    return result;
}

int32_t fake_nxsend(nxSOCKET, const void*, int32_t len, int) {
    ++g_state.send_calls;

    int32_t result = len;
    if (g_state.send_result_index < g_state.send_results.size()) {
        result = g_state.send_results[g_state.send_result_index++];
    }

    if (result < 0) {
        g_state.last_error = g_state.send_error;
    }

    return result;
}

int fake_nxselect(int, nxfd_set*, nxfd_set*, nxfd_set*, nxtimeval*) {
    ++g_state.select_calls;
    return g_state.select_result;
}

struct xnet_tcp_socket_async_fixture : ::testing::Test {
    std::unique_ptr<vsomeip_v3::xnet_api::api_table> original_table_;
    boost::asio::io_context io_;

    void SetUp() override {
        g_state.reset();
        original_table_ = std::make_unique<vsomeip_v3::xnet_api::api_table>(vsomeip_v3::xnet_api::get_api_table());

        auto table = *original_table_;
        table.nxgetlasterrornum_fn = &fake_nxgetlasterrornum;
        table.nxsocket_fn = &fake_nxsocket;
        table.nxsetsockopt_fn = &fake_nxsetsockopt;
        table.nxclose_fn = &fake_nxclose;
        table.nxconnect_fn = &fake_nxconnect;
        table.nxgetsockopt_fn = &fake_nxgetsockopt;
        table.nxrecv_fn = &fake_nxrecv;
        table.nxsend_fn = &fake_nxsend;
        table.nxselect_fn = &fake_nxselect;
        vsomeip_v3::xnet_api::set_api_table_for_test(table);
    }

    void TearDown() override {
        if (original_table_) {
            vsomeip_v3::xnet_api::set_api_table_for_test(*original_table_);
        }
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

    std::unique_ptr<vsomeip_v3::tcp_socket> create_open_socket() {
        auto socket = std::unique_ptr<vsomeip_v3::tcp_socket>(std::make_unique<vsomeip_v3::xnet_tcp_socket>(io_, reinterpret_cast<nxIpStackRef_t>(0x1)));
        boost::system::error_code ec;
        socket->open(boost::asio::ip::tcp::v4(), ec);
        EXPECT_FALSE(ec);
        return socket;
    }
};

}

TEST_F(xnet_tcp_socket_async_fixture, async_connect_success_calls_handler_once) {
    auto socket = create_open_socket();

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;

    socket->async_connect(
        boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 33001),
        [&](boost::system::error_code const& ec) {
            ++handler_calls;
            completion_ec = ec;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(300));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_EQ(g_state.connect_calls, 1);
}

TEST_F(xnet_tcp_socket_async_fixture, async_connect_would_block_waits_and_checks_so_error) {
    auto socket = create_open_socket();

    g_state.connect_result = -1;
    g_state.connect_error = 13837;
    g_state.select_result = 1;
    g_state.so_error_value = 0;

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;

    socket->async_connect(
        boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 33002),
        [&](boost::system::error_code const& ec) {
            ++handler_calls;
            completion_ec = ec;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(300));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_GE(g_state.select_calls, 1);
    EXPECT_GE(g_state.so_error_queries, 1);
}

TEST_F(xnet_tcp_socket_async_fixture, async_receive_would_block_then_success_copies_payload) {
    auto socket = create_open_socket();

    g_state.recv_results = {-1, 3};
    g_state.recv_error = 13837;
    g_state.select_result = 1;
    g_state.recv_payload = {0x44, 0x55, 0x66};

    std::array<std::uint8_t, 8> buffer{};

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;
    std::size_t completion_bytes{0};

    socket->async_receive(
        boost::asio::buffer(buffer.data(), buffer.size()),
        [&](boost::system::error_code const& ec, std::size_t bytes) {
            ++handler_calls;
            completion_ec = ec;
            completion_bytes = bytes;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(350));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_EQ(completion_bytes, 3U);
    EXPECT_EQ(buffer[0], 0x44);
    EXPECT_EQ(buffer[1], 0x55);
    EXPECT_EQ(buffer[2], 0x66);
    EXPECT_GE(g_state.recv_calls, 2);
    EXPECT_GE(g_state.select_calls, 1);
}

TEST_F(xnet_tcp_socket_async_fixture, async_receive_not_connected_reports_eof) {
    auto socket = create_open_socket();

    g_state.recv_results = {-1};
    g_state.recv_error = 13858;

    std::array<std::uint8_t, 8> buffer{};
    xnet_test_utils::async_completion_probe probe;
    boost::system::error_code completion_ec;
    std::size_t completion_bytes{1};

    socket->async_receive(
        boost::asio::buffer(buffer.data(), buffer.size()),
        [&](boost::system::error_code const& ec, std::size_t bytes) {
            completion_ec = ec;
            completion_bytes = bytes;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(300));

    EXPECT_EQ(probe.signal_count(), 1U);
    xnet_test_utils::expect_error_value(completion_ec, boost::asio::error::eof);
    EXPECT_EQ(completion_bytes, 0U);
    EXPECT_EQ(g_state.recv_calls, 1);
}

TEST_F(xnet_tcp_socket_async_fixture, async_write_sequence_handles_partial_sends) {
    auto socket = create_open_socket();

    g_state.send_results = {2, 1, 4};

    std::array<std::uint8_t, 3> b1{{1, 2, 3}};
    std::array<std::uint8_t, 4> b2{{4, 5, 6, 7}};
    std::vector<boost::asio::const_buffer> buffers;
    buffers.emplace_back(boost::asio::buffer(b1.data(), b1.size()));
    buffers.emplace_back(boost::asio::buffer(b2.data(), b2.size()));

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;
    std::size_t completion_bytes{0};

    socket->async_write(
        buffers,
        [&](boost::system::error_code const& ec, std::size_t bytes) {
            ++handler_calls;
            completion_ec = ec;
            completion_bytes = bytes;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(350));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_EQ(completion_bytes, b1.size() + b2.size());
    EXPECT_EQ(g_state.send_calls, 3);
}

TEST_F(xnet_tcp_socket_async_fixture, async_write_with_completion_condition_stops_early) {
    auto socket = create_open_socket();

    g_state.send_results = {2, 2, 2};

    std::array<std::uint8_t, 6> payload{{9, 8, 7, 6, 5, 4}};
    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;
    std::size_t completion_bytes{0};

    socket->async_write(
        boost::asio::buffer(payload.data(), payload.size()),
        [](boost::system::error_code const&, std::size_t transferred) {
            return transferred >= 4U ? 0U : 1U;
        },
        [&](boost::system::error_code const& ec, std::size_t bytes) {
            ++handler_calls;
            completion_ec = ec;
            completion_bytes = bytes;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(350));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_EQ(completion_bytes, 4U);
    EXPECT_EQ(g_state.send_calls, 2);
}

TEST_F(xnet_tcp_socket_async_fixture, async_connect_cancel_race_reports_operation_aborted) {
    auto socket = create_open_socket();

    g_state.connect_result = -1;
    g_state.connect_error = 13837;
    g_state.select_result = 0;

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;

    socket->async_connect(
        boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 33003),
        [&](boost::system::error_code const& ec) {
            ++handler_calls;
            completion_ec = ec;
            probe.signal();
        });

    auto const cancel_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (g_state.select_calls == 0 && std::chrono::steady_clock::now() < cancel_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    boost::system::error_code cancel_ec;
    socket->cancel(cancel_ec);
    xnet_test_utils::expect_success(cancel_ec);

    pump_until(probe, std::chrono::milliseconds(350));

    EXPECT_EQ(handler_calls.load(), 1);
    EXPECT_EQ(completion_ec, boost::asio::error::make_error_code(boost::asio::error::operation_aborted));
}