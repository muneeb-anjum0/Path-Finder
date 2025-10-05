#pragma once

#include "common.h"

// Logging functions
void open_log();
void logf(const char *fmt, ...);
[[noreturn]] void fatal(const char *fmt, ...);