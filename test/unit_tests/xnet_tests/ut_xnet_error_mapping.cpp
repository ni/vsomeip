#include <gtest/gtest.h>

#include <boost/asio/error.hpp>
#include <boost/system/errc.hpp>

#include "xnet_error.hpp"

TEST(xnet_error_mapping_test, maps_13850_and_neg_13850_to_address_not_available) {
    const auto positive = vsomeip_v3::xnet_to_boost_error(13850);
    const auto negative = vsomeip_v3::xnet_to_boost_error(-13850);
    const auto expected = boost::system::errc::make_error_code(boost::system::errc::address_not_available);

    EXPECT_EQ(positive, expected);
    EXPECT_EQ(negative, expected);
}

TEST(xnet_error_mapping_test, maps_would_block_family_codes_to_would_block) {
    const auto expected = boost::asio::error::make_error_code(boost::asio::error::would_block);

    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13837), expected);
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13811), expected);
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(-13837), expected);
}

TEST(xnet_error_mapping_test, maps_bad_descriptor_family_codes_to_bad_descriptor) {
    const auto expected = boost::asio::error::make_error_code(boost::asio::error::bad_descriptor);

    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13809), expected);
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(-13809), expected);
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(-13008), expected);
}

TEST(xnet_error_mapping_test, maps_connection_and_network_related_codes) {
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13849), boost::asio::error::make_error_code(boost::asio::error::address_in_use));
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13852), boost::asio::error::make_error_code(boost::asio::error::network_unreachable));
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13864), boost::asio::error::make_error_code(boost::asio::error::host_unreachable));
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13854), boost::asio::error::make_error_code(boost::asio::error::connection_aborted));
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13855), boost::asio::error::make_error_code(boost::asio::error::connection_reset));
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13858), boost::asio::error::make_error_code(boost::asio::error::not_connected));
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13860), boost::asio::error::make_error_code(boost::asio::error::timed_out));
    EXPECT_EQ(vsomeip_v3::xnet_to_boost_error(13861), boost::asio::error::make_error_code(boost::asio::error::connection_refused));
}

TEST(xnet_error_mapping_test, maps_zero_to_success_error_code) {
    const auto ec = vsomeip_v3::xnet_to_boost_error(0);
    EXPECT_FALSE(ec);
    EXPECT_EQ(ec.value(), 0);
}

TEST(xnet_error_mapping_test, unknown_code_falls_back_to_generic_category_with_same_value) {
    constexpr int k_unknown = 424242;
    const auto ec = vsomeip_v3::xnet_to_boost_error(k_unknown);

    EXPECT_EQ(ec.value(), k_unknown);
    EXPECT_EQ(ec.category(), boost::system::generic_category());
}