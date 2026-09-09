#include "../include/xnet_api.hpp"

#if defined(VSOMEIP_ENABLE_XNET)

#include <atomic>
#include <memory>

namespace vsomeip_v3::xnet_api {

namespace {

using api_table_ptr = std::shared_ptr<const api_table>;

// Holds the active api_table behind an atomic shared_ptr. Readers take an
// immutable snapshot (load) while writers atomically swap in a freshly built
// table (store). This removes the data race between get_api_table() and the
// test seam setters without locking the hot read path.
std::atomic<api_table_ptr>& api_table_slot() {
    static std::atomic<api_table_ptr> slot{std::make_shared<const api_table>()};
    return slot;
}

} // namespace

api_table get_api_table() {
    return *api_table_slot().load(std::memory_order_acquire);
}

void set_api_table_for_test(const api_table& _table) {
    api_table_slot().store(std::make_shared<const api_table>(_table), std::memory_order_release);
}

void reset_api_table_for_test() {
    api_table_slot().store(std::make_shared<const api_table>(), std::memory_order_release);
}

} // namespace vsomeip_v3::xnet_api

#endif // VSOMEIP_ENABLE_XNET
