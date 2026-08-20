/**
 * @file log.cpp
 * @brief stderr / log-file diagnostics.
 */

#include "vidwizard/log.hpp"

#include <cstdio>
#include <mutex>

namespace vidwizard
{
namespace
{

std::mutex g_log_mu{};
bool g_verbose = false;
FILE *g_log_file = nullptr;

void emit(FILE *fp, const char *fmt, va_list args)
{
    if (fp == nullptr || fmt == nullptr)
    {
        return;
    }
    std::vfprintf(fp, fmt, args);
    std::fputc('\n', fp);
    std::fflush(fp);
}

} // namespace

void set_verbose(bool on)
{
    std::lock_guard<std::mutex> lock(g_log_mu);
    g_verbose = on;
}

int set_log_file(const std::filesystem::path &path)
{
    std::lock_guard<std::mutex> lock(g_log_mu);
    if (g_log_file != nullptr)
    {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
    if (path.empty())
    {
        return 0;
    }
    g_log_file = std::fopen(path.c_str(), "w");
    if (g_log_file == nullptr)
    {
        return -1;
    }
    return 0;
}

void close_log_file(void)
{
    std::lock_guard<std::mutex> lock(g_log_mu);
    if (g_log_file != nullptr)
    {
        std::fclose(g_log_file);
        g_log_file = nullptr;
    }
}

void log_errorv(const char *fmt, va_list args)
{
    std::lock_guard<std::mutex> lock(g_log_mu);
    va_list copy;
    va_copy(copy, args);
    emit(stderr, fmt, args);
    if (g_log_file != nullptr)
    {
        emit(g_log_file, fmt, copy);
    }
    va_end(copy);
}

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_errorv(fmt, args);
    va_end(args);
}

void log_verbose(const char *fmt, ...)
{
    va_list args;
    bool on = false;
    {
        std::lock_guard<std::mutex> lock(g_log_mu);
        on = g_verbose;
    }
    if (!on)
    {
        return;
    }
    va_start(args, fmt);
    log_errorv(fmt, args);
    va_end(args);
}

} // namespace vidwizard
