#include "../include/xnet_error.hpp"
#include "../include/xnet_api.hpp"

#include <boost/asio/error.hpp>
#include <boost/system/errc.hpp>
#include <cerrno>

#include "nxsocket.h"

namespace vsomeip_v3 {

int xnet_get_last_error() {
    return static_cast<int>(xnet_api::nxgetlasterrornum());
}

boost::system::error_code xnet_to_boost_error(int _xnet_error) {
    if (_xnet_error == 0) {
        return {};
    }

// XNET reports stack-native error numbers via nxgetlasterrornum().
    // These are NI XNET specific codes (nxE* from nxsocket_errors.h), NOT standard errno values.
    // Both positive (nxE*) and negative (nxIpStackErr*) variants are mapped.
    switch (_xnet_error) {
    case -13008: // nxIpStackErrInvalidSocket (0xFFFFCD30)
        return boost::asio::error::make_error_code(boost::asio::error::bad_descriptor);
    case 13809:  // nxEBADF (0x000035F1)
    case -13809: // nxIpStackErrBadFile (0xFFFFCA0F)
        return boost::asio::error::make_error_code(boost::asio::error::bad_descriptor);
    case 13837:  // nxEWOULDBLOCK (0x0000360D)
    case -13837: // nxIpStackErrWouldBlock (0xFFFFC9F3)
    case 13811:  // nxEAGAIN (0x000035F3)
        return boost::asio::error::make_error_code(boost::asio::error::would_block);
    case 13838:  // nxEINPROGRESS (0x0000360E)
    case -13839: // nxIpStackErrAlreadyInProgress (0xFFFFC9F1)
        return boost::asio::error::make_error_code(boost::asio::error::in_progress);
    case 13839:  // nxEALREADY (0x0000360F)
        return boost::asio::error::make_error_code(boost::asio::error::already_started);
    case 13804:  // nxEINTR (0x000035EC)
        return boost::asio::error::make_error_code(boost::asio::error::interrupted);
    case 13813:  // nxEACCES (0x000035F5)
        return boost::asio::error::make_error_code(boost::asio::error::access_denied);
    case 13849:  // nxEADDRINUSE (0x00003619)
        return boost::asio::error::make_error_code(boost::asio::error::address_in_use);
    case 13850:  // nxEADDRNOTAVAIL (0x0000361A)
    case -13850: // nxIpStackErrAddressNotAvailable (vendor-specific negative form)
        return boost::system::errc::make_error_code(boost::system::errc::address_not_available);
    case 13852:  // nxENETUNREACH (0x0000361C)
        return boost::asio::error::make_error_code(boost::asio::error::network_unreachable);
    case 13864:  // nxEHOSTUNREACH (0x00003628)
    case -13864: // nxIpStackErrHostUnreachable (0xFFFFC9D8)
        return boost::asio::error::make_error_code(boost::asio::error::host_unreachable);
    case 13854:  // nxECONNABORTED (0x0000361E)
        return boost::asio::error::make_error_code(boost::asio::error::connection_aborted);
    case 13855:  // nxECONNRESET (0x0000361F)
        return boost::asio::error::make_error_code(boost::asio::error::connection_reset);
    case 13856:  // nxENOBUFS (0x00003620)
        return boost::asio::error::make_error_code(boost::asio::error::no_buffer_space);
    case 13857:  // nxEISCONN (0x00003621)
    case -13857: // nxIpStackErrAlreadyConnected (0xFFFFC9DF)
        return boost::asio::error::make_error_code(boost::asio::error::already_connected);
    case 13858:  // nxENOTCONN (0x00003622)
    case -13858: // nxIpStackErrNotConnected (0xFFFFC9DE)
        return boost::asio::error::make_error_code(boost::asio::error::not_connected);
    case 13860:  // nxETIMEDOUT (0x00003624)
        return boost::asio::error::make_error_code(boost::asio::error::timed_out);
    case 13861:  // nxECONNREFUSED (0x00003625)
        return boost::asio::error::make_error_code(boost::asio::error::connection_refused);
    case 13842:  // nxEMSGSIZE (0x00003612)
        return boost::asio::error::make_error_code(boost::asio::error::message_size);
    case -13836: // nxIpStackErrWouldDeadlock (0xFFFFC9F4)
        return boost::asio::error::make_error_code(boost::asio::error::would_block);
    }

    return boost::system::error_code(_xnet_error, boost::system::generic_category());
}

} // namespace vsomeip_v3
