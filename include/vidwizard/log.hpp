/**
 * @file log.hpp
 * @brief stderr (+ optional log file) diagnostics; never stdout.
 */
#pragma once

#include <cstdarg>
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

} // namespace vidwizard
