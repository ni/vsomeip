#!/usr/bin/echo non-executable-shell-library
# shellcheck shell=bash
#
# Canonical list of fixed SOME/IP network-test ports that fall inside the
# kernel's ephemeral port range (net.ipv4.ip_local_port_range, typically
# 32768-60999).
#
# These are reserved via net.ipv4.ip_local_reserved_ports so the kernel never
# hands them out as ephemeral source ports for outbound connections, in order
# to avoid collisions with the SOME/IP servers that bind to them.
#
# Reserving the ports does not prevent the tests from explicitly binding them.
#
# This file is the single source of truth. It is sourced by:
#   - test/network_tests/conf/run_isolated.sh   (per-test master sandbox netns)
#   - zuul/network-tests/entrypoint-slave       (slave container netns)
RESERVED_TEST_PORTS="34500-34600,60000,60666"
