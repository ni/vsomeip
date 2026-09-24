// Copyright (C) 2014-2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../include/abstract_socket_factory.hpp"
#include "../include/asio_socket_factory.hpp"

#include "logger_ext.hpp"

#include <mutex>
#include <stdexcept>

#define VSOMEIP_LOG_PREFIX "asf"

namespace vsomeip_v3 {

/**
 * Q: Why is this not a static local variable in the init function itself?
 * A: Because we need the capability of injecting a fake abstract socket factory,
 *    to swap out the asio sockets.
 *
 * Q: Why is there no mutex?
 * A: Because once the abstract_socket_factory::get() has been called for the first
 *    time the used shared_ptr of the running binary will no longer be changed.
 *
 * Q: But what if abstract_socket_factory::get() would be called from multiple threads?
 * A: This is fine, as the init() function will be called exactly once and is awaited,
 *    by secondary calls of ::get(), if the init is not yet done.
 *
 * Q: But what if set_abstract_factory is called from another thread as ::get()?
 * A: This is most likely the case. But this should only happen in test code,
 *    within production code "set_abstract_factory" should not be called.
 *
 * Q: But why would this be fine in test code?
 * A: Because within test code the developer can be requested to ensure that before
 *    the spawning of any thread set had been called already (notice this is only meaningfully done
 *    once per binary).
 *
 * Q: Wouldn't it be more convinient to allow the adjustment of the shared_ptr at any time?
 * A: Maybe. But it would come along the cost of mutual exclusive access, which was considered a
 *non-neglectable cost to pay during production run-time for enabling these tests.
 **/
static std::shared_ptr<abstract_socket_factory> _factory;
// --- NI modification: BEGIN ---
// Synchronize factory initialization and detect injection after selection is frozen.
static bool _factory_finalized{false};
static bool _late_injection_detected{false};
static std::mutex _factory_mutex;

static std::shared_ptr<abstract_socket_factory> init() {
    std::scoped_lock its_lock{_factory_mutex};
    if (!_factory) {
        VSOMEIP_INFO_P << "socket_factory_freeze=default_asio";
        _factory = std::make_shared<asio_socket_factory>();
    } else {
        VSOMEIP_INFO_P << "socket_factory_freeze=preinjected";
    }
    _factory_finalized = true;
    return _factory;
}

void set_abstract_factory(std::shared_ptr<abstract_socket_factory> ptr) {
std::scoped_lock its_lock{_factory_mutex};
// --- NI modification: BEGIN ---
// Re-registering the identical factory pointer is a no-op and must stay tolerated:
// the gtest fixtures inject from SetUpTestSuite(), which runs once per test suite
// rather than once per binary. In unit_tests_endpoint_tests the first suite to call
// abstract_socket_factory::get() froze the selection, so the next suite aborted the
// process with "Late socket factory injection detected after factory freeze.".
// Injecting the same pointer again cannot leave a previously returned get() pointer
// stale, hence the early return. Injecting a *different* factory after the freeze
// still sets the late-injection flag and throws.
if (_factory_finalized && _factory == ptr) {
    return;
}
// --- NI modification: END ---
if (_factory_finalized) {
        _late_injection_detected = true;
        VSOMEIP_ERROR_P << "late socket factory injection detected after factory freeze.";
        throw std::runtime_error("Late socket factory injection detected after factory freeze.");
    }
    _factory = ptr;
}

void freeze_abstract_factory() {
    std::scoped_lock its_lock{_factory_mutex};
    if (_factory_finalized) {
        return;
    }

    if (!_factory) {
        VSOMEIP_INFO_P << "socket_factory_freeze=default_asio";
        _factory = std::make_shared<asio_socket_factory>();
    } else {
        VSOMEIP_INFO_P << "socket_factory_freeze=preinjected";
    }

    _factory_finalized = true;
}

abstract_socket_factory* abstract_socket_factory::get() {
    static auto const factory = init();
    return factory.get();
}

bool is_abstract_factory_finalized() {
    return _factory_finalized;
}

bool was_abstract_factory_late_injection_detected() {
    return _late_injection_detected;
}
// --- NI modification: END ---

}
