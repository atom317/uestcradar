#pragma once

#include <csignal>

int run_telemetry_exporter(volatile std::sig_atomic_t& running);
