#pragma once

#include <cstddef>
#include <string>

// Append-only file logger with a background writer thread. Producers pay only a
// snprintf + a brief lock; the actual write() happens off-thread so the game/
// physics loops never block on disk I/O. The writer uses raw write() (not
// buffered stdio), so everything it has flushed survives a process crash —
// which is the point: pair this with crash_handler to debug "the client just
// vanished" reports.

#if defined(__GNUC__) || defined(__clang__)
#define DEBUG_LOG_PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))
#else
#define DEBUG_LOG_PRINTF_FMT(a, b)
#endif

namespace shared::debug_log {

// Opens `path` for append and starts the writer thread. Idempotent; returns
// false (and logging stays a no-op) if the file cannot be opened.
bool init(const std::string& path);

// Drains the queue, stops the writer, closes the file. Safe to call twice.
void shutdown();

// Enqueue a formatted line (a timestamp prefix and trailing newline are added).
void logf(const char* fmt, ...) DEBUG_LOG_PRINTF_FMT(1, 2);

// Direct, lock-free, async-signal-safe write straight to the log fd, bypassing
// the queue. For the crash handler only — ordinary code uses LOG_FILE.
void writeRaw(const char* data, size_t len);

// Underlying OS file descriptor (POSIX fd / CRT fd on Windows), or -1.
int fd();

}  // namespace shared::debug_log

#define LOG_FILE(...) ::shared::debug_log::logf(__VA_ARGS__)
