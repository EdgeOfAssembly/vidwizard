/**
 * @file log.hpp
 * @brief stderr (+ optional log file) diagnostics; never stdout.
 */
#pragma once

#include <cstdarg>
#include <cstdint>
#include <filesystem>

namespace vidwizard
{

/**
 * @brief Enable extra progress lines.
 *
 * @param[in] on Non-zero to print verbose messages.
 */
void set_verbose(bool on);

/**
 * @brief Duplicate diagnostics to @p path (in addition to stderr).
 *
 * @param[in] path Log file path; empty closes a previous log file.
 *
 * @return 0 on success, -1 if the file could not be opened.
 */
int set_log_file(const std::filesystem::path &path);

/**
 * @brief Close the optional log file (safe to call more than once).
 */
void close_log_file(void);

/**
 * @brief Always-on diagnostic (errors, warnings, progress).
 *
 * @param[in] fmt printf-style format; must not be NULL.
 */
void log_error(const char *fmt, ...);

/**
 * @brief Verbose-only diagnostic.
 *
 * @param[in] fmt printf-style format; must not be NULL.
 */
void log_verbose(const char *fmt, ...);

/**
 * @brief va_list form of log_error.
 *
 * @param[in] fmt  printf-style format.
 * @param[in] args Variable arguments.
 */
void log_errorv(const char *fmt, va_list args);

/**
 * @brief Rate-limited in-place progress (`vidwizard: frame N`) on stderr.
 *
 * Overwrites the previous progress line with a carriage return. Prints at
 * most about once per second (the first frame always prints). Call
 * log_progress_done() when the operation finishes.
 *
 * @param[in] frame 1-based frame count so far.
 *
 * @thread_safety Serialized with other log_* calls.
 */
void log_progress_frame(int64_t frame);

/**
 * @brief Finish an in-place progress line (final count + newline).
 *
 * Safe to call when no progress was printed. A following log_error() also
 * ends the line so diagnostics are not stuck on the progress row.
 */
void log_progress_done(void);

} // namespace vidwizard
