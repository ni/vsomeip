#include <gtest/gtest.h>

#include <boost/endian/conversion.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "xnet_api.hpp"
#include "udp_socket.hpp"
#include "xnet_udp_socket.hpp"
#include "common/xnet_test_utils.hpp"

namespace {

struct fake_udp_async_backend_state {
    nxSOCKET socket_to_return{static_cast<nxSOCKET>(2233)};
    int last_error{0};

    int connect_result{0};
    int connect_calls{0};

    int recvfrom_result{0};
    int recvfrom_calls{0};
    std::vector<std::uint8_t> recv_payload{};
    boost::asio::ip::udp::endpoint recv_remote_ep{boost::asio::ip::make_address_v4("127.0.0.2"), 40001};

    int send_result{0};
    int send_calls{0};

    int sendto_result{0};
    int sendto_calls{0};

    int select_result{1};
    int select_calls{0};

    int open_setopt_calls{0};

    void reset() { *this = fake_udp_async_backend_state{}; }
};

fake_udp_async_backend_state g_state;

auto fake_nxgetlasterrornum() {
    return static_cast<decltype(vsomeip_v3::xnet_api::nxgetlasterrornum())>(g_state.last_error);
}

nxSOCKET fake_nxsocket(nxIpStackRef_t, int, int, int) {
    return g_state.socket_to_return;
}

int32_t fake_nxsetsockopt(nxSOCKET, int level, int name, const void*, nxsocklen_t) {
    if (level == nxSOL_SOCKET && name == nxSO_NONBLOCK) {
        ++g_state.open_setopt_calls;
    }
    return 0;
}

int fake_nxclose(nxSOCKET) {
    return 0;
}

int32_t fake_nxconnect(nxSOCKET, const nxsockaddr*, nxsocklen_t) {
    ++g_state.connect_calls;
    if (g_state.connect_result == -1) {
        g_state.last_error = 13837; // would_block
    }
    return g_state.connect_result;
}

int32_t fake_nxrecvfrom(nxSOCKET, void* buffer, int32_t len, int, nxsockaddr* from, nxsocklen_t* from_len) {
    ++g_state.recvfrom_calls;
    if (g_state.recvfrom_result >= 0) {
        const auto copy_len = static_cast<std::size_t>(std::min<int32_t>(g_state.recvfrom_result, len));
        if (copy_len > 0 && buffer != nullptr && !g_state.recv_payload.empty()) {
            std::memcpy(buffer, g_state.recv_payload.data(), std::min(copy_len, g_state.recv_payload.size()));
        }

        if (from != nullptr && from_len != nullptr && *from_len >= static_cast<nxsocklen_t>(sizeof(nxsockaddr_in))) {
            auto* out = reinterpret_cast<nxsockaddr_in*>(from);
            std::memset(out, 0, sizeof(nxsockaddr_in));
            out->sin_family = nxAF_INET;
            out->sin_port = boost::endian::native_to_big(g_state.recv_remote_ep.port());
            out->sin_addr.addr = boost::endian::native_to_big(g_state.recv_remote_ep.address().to_v4().to_uint());
            *from_len = static_cast<nxsocklen_t>(sizeof(nxsockaddr_in));
        }
        return g_state.recvfrom_result;
    }

    return -1;
}

int32_t fake_nxsend(nxSOCKET, const void*, int32_t len, int) {
    ++g_state.send_calls;
    return (g_state.send_result >= 0) ? g_state.send_result : len;
}

int32_t fake_nxsendto(nxSOCKET, const void*, int32_t len, int32_t, const nxsockaddr*, nxsocklen_t) {
    ++g_state.sendto_calls;
    return (g_state.sendto_result >= 0) ? g_state.sendto_result : len;
}

int fake_nxselect(int, nxfd_set*, nxfd_set*, nxfd_set*, nxtimeval*) {
    ++g_state.select_calls;
    return g_state.select_result;
}

struct xnet_udp_socket_async_fixture : ::testing::Test {
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
        table.nxrecvfrom_fn = &fake_nxrecvfrom;
        table.nxsend_fn = &fake_nxsend;
        table.nxsendto_fn = &fake_nxsendto;
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

    std::unique_ptr<vsomeip_v3::udp_socket> create_open_udp_socket() {
        auto socket = std::unique_ptr<vsomeip_v3::udp_socket>(std::make_unique<vsomeip_v3::xnet_udp_socket>(io_, reinterpret_cast<nxIpStackRef_t>(0x1)));
        boost::system::error_code ec;
        socket->open(boost::asio::ip::udp::v4(), ec);
        EXPECT_FALSE(ec);
        return socket;
    }
};

}

TEST_F(xnet_udp_socket_async_fixture, async_connect_success_calls_handler_once) {
    auto socket = create_open_udp_socket();

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;

    socket->async_connect(
        boost::asio::ip::udp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 30001),
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

TEST_F(xnet_udp_socket_async_fixture, async_connect_would_block_uses_select_and_completes_success) {
    auto socket = create_open_udp_socket();

    g_state.connect_result = -1;
    g_state.select_result = 1;

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;

    socket->async_connect(
        boost::asio::ip::udp::endpoint(boost::asio::ip::make_address_v4("127.0.0.1"), 30002),
        [&](boost::system::error_code const& ec) {
            ++handler_calls;
            completion_ec = ec;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(300));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_GE(g_state.select_calls, 1);
}

TEST_F(xnet_udp_socket_async_fixture, async_receive_from_success_copies_payload_and_remote_endpoint) {
    auto socket = create_open_udp_socket();

    g_state.recv_payload = {0x11, 0x22, 0x33};
    g_state.recvfrom_result = static_cast<int>(g_state.recv_payload.size());

    std::array<std::uint8_t, 8> recv_buffer{};
    boost::asio::ip::udp::endpoint remote;

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;
    std::size_t completion_bytes{0};

    socket->async_receive_from(
        boost::asio::buffer(recv_buffer.data(), recv_buffer.size()), remote,
        [&](boost::system::error_code const& ec, std::size_t bytes) {
            ++handler_calls;
            completion_ec = ec;
            completion_bytes = bytes;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(300));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_EQ(completion_bytes, g_state.recv_payload.size());
    EXPECT_EQ(recv_buffer[0], 0x11);
    EXPECT_EQ(recv_buffer[1], 0x22);
    EXPECT_EQ(recv_buffer[2], 0x33);
    xnet_test_utils::expect_udp_endpoint_eq(remote, g_state.recv_remote_ep);
}

TEST_F(xnet_udp_socket_async_fixture, async_receive_from_bad_descriptor_is_mapped_to_operation_aborted) {
    auto socket = create_open_udp_socket();

    g_state.recvfrom_result = -1;
    g_state.last_error = 13809; // bad_descriptor

    std::array<std::uint8_t, 4> recv_buffer{};
    boost::asio::ip::udp::endpoint remote;

    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;
    std::size_t completion_bytes{42};

    socket->async_receive_from(
        boost::asio::buffer(recv_buffer.data(), recv_buffer.size()), remote,
        [&](boost::system::error_code const& ec, std::size_t bytes) {
            ++handler_calls;
            completion_ec = ec;
            completion_bytes = bytes;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(300));

    EXPECT_EQ(handler_calls.load(), 1);
    EXPECT_EQ(completion_ec, boost::asio::error::make_error_code(boost::asio::error::operation_aborted));
    EXPECT_EQ(completion_bytes, 0U);
}

TEST_F(xnet_udp_socket_async_fixture, async_send_reports_sent_bytes_and_calls_handler_once) {
    auto socket = create_open_udp_socket();

    g_state.send_result = 5;

    std::array<std::uint8_t, 5> payload{{1, 2, 3, 4, 5}};
    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;
    std::size_t completion_bytes{0};

    socket->async_send(
        boost::asio::buffer(payload.data(), payload.size()),
        [&](boost::system::error_code const& ec, std::size_t bytes) {
            ++handler_calls;
            completion_ec = ec;
            completion_bytes = bytes;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(300));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_EQ(completion_bytes, payload.size());
    EXPECT_EQ(g_state.send_calls, 1);
}

TEST_F(xnet_udp_socket_async_fixture, async_send_to_reports_sent_bytes_and_calls_handler_once) {
    auto socket = create_open_udp_socket();

    g_state.sendto_result = 4;

    std::array<std::uint8_t, 4> payload{{9, 8, 7, 6}};
    xnet_test_utils::async_completion_probe probe;
    std::atomic<int> handler_calls{0};
    boost::system::error_code completion_ec;
    std::size_t completion_bytes{0};

    socket->async_send_to(
        boost::asio::buffer(payload.data(), payload.size()),
        boost::asio::ip::udp::endpoint(boost::asio::ip::make_address_v4("127.0.0.9"), 31000),
        [&](boost::system::error_code const& ec, std::size_t bytes) {
            ++handler_calls;
            completion_ec = ec;
            completion_bytes = bytes;
            probe.signal();
        });

    pump_until(probe, std::chrono::milliseconds(300));

    EXPECT_EQ(handler_calls.load(), 1);
    xnet_test_utils::expect_success(completion_ec);
    EXPECT_EQ(completion_bytes, payload.size());
    EXPECT_EQ(g_state.sendto_calls, 1);
}
