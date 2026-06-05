#pragma once

#include <string>

// Installs OS-level fatal-fault handlers that dump a banner + stack backtrace
// to the debug_log file (and stderr) before the process dies, then let the OS
// run its normal crash path. Turns a silently vanishing process into a logged
// stack trace. Call AFTER debug_log::init(). Covers POSIX signals
// (SIGSEGV/SIGABRT/ SIGBUS/SIGFPE/SIGILL) and, on Windows, the
// unhandled-exception filter; plus std::terminate (uncaught C++ exceptions) on
// both.

namespace shared::crash_handler {

void install(const std::string& appName);

}  // namespace shared::crash_handler
