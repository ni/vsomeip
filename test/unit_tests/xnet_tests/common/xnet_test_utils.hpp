#pragma once

#include <gtest/gtest.h>

#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace xnet_test_utils {

class async_completion_probe {
public:
    void signal() {
        {
            std::lock_guard<std::mutex> its_lock(mutex_);
            ++signals_;
        }
        cv_.notify_all();
    }

    template<typename Rep, typename Period>
    bool wait_for(std::chrono::duration<Rep, Period> _timeout, std::size_t _expected_signals = 1U) {
        std::unique_lock<std::mutex> its_lock(mutex_);
        return cv_.wait_for(its_lock, _timeout, [this, _expected_signals]() { return signals_ >= _expected_signals; });
    }

    std::size_t signal_count() const {
        std::lock_guard<std::mutex> its_lock(mutex_);
        return signals_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t signals_{0U};
};

inline void expect_success(boost::system::error_code const& _ec) {
    EXPECT_FALSE(_ec) << "Unexpected error: value=" << _ec.value() << " message='" << _ec.message() << "'";
}

inline void expect_error_value(boost::system::error_code const& _ec, int _value) {
    EXPECT_TRUE(_ec);
    EXPECT_EQ(_ec.value(), _value) << "Unexpected error message='" << _ec.message() << "'";
}

template <typename TExpected, typename TBase>
inline void expect_dynamic_type(TBase* _ptr) {
    EXPECT_NE(dynamic_cast<TExpected*>(_ptr), nullptr);
}

inline void expect_udp_endpoint_eq(boost::asio::ip::udp::endpoint const& _actual,
                                   boost::asio::ip::udp::endpoint const& _expected) {
    EXPECT_EQ(_actual.address(), _expected.address());
    EXPECT_EQ(_actual.port(), _expected.port());
}

class call_sequence_recorder {
public:
    void push(std::string _name) {
        std::lock_guard<std::mutex> its_lock(mutex_);
        sequence_.push_back(std::move(_name));
    }

    std::vector<std::string> snapshot() const {
        std::lock_guard<std::mutex> its_lock(mutex_);
        return sequence_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> sequence_;
};

}
