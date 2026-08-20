/**
 * @file log.cpp
 * @brief stderr / log-file diagnostics.
 */

#include "vidwizard/log.hpp"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace vidwizard
{
namespace
{

std::mutex g_log_mu{};
bool g_verbose = false;
FILE *g_log_file = nullptr;
bool g_progress_active = false;
int g_progress_width = 0;
int64_t g_progress_frame = -1;
std::chrono::steady_clock::time_point g_progress_tp{};

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

void write_progress_line_locked(int64_t frame)
{
    char buf[128];
    const int n = std::snprintf(buf, sizeof(buf), "vidwizard: frame %lld",
                                static_cast<long long>(frame));
    if (n < 0)
    {
        return;
    }
    const int len = (n < static_cast<int>(sizeof(buf))) ? n : static_cast<int>(sizeof(buf) - 1U);
    std::fputc('\r', stderr);
    std::fputs(buf, stderr);
    if (len < g_progress_width)
    {
        for (int i = len; i < g_progress_width; i++)
        {
            std::fputc(' ', stderr);
        }
    }
    else
    {
        g_progress_width = len;
    }
    g_progress_active = true;
    std::fflush(stderr);
    if (g_log_file != nullptr)
    {
        std::fputs(buf, g_log_file);
        std::fputc('\n', g_log_file);
        std::fflush(g_log_file);
    }
}

void end_progress_line_locked(void)
{
    if (g_progress_active)
    {
        std::fputc('\n', stderr);
        std::fflush(stderr);
        g_progress_active = false;
        g_progress_width = 0;
    }
    g_progress_frame = -1;
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
    end_progress_line_locked();
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

void log_progress_frame(int64_t frame)
{
    std::lock_guard<std::mutex> lock(g_log_mu);
    g_progress_frame = frame;
    const auto now = std::chrono::steady_clock::now();
    if (g_progress_active && (now - g_progress_tp) < std::chrono::seconds(1))
    {
        return;
    }
    g_progress_tp = now;
    write_progress_line_locked(frame);
}

void log_progress_done(void)
{
    std::lock_guard<std::mutex> lock(g_log_mu);
    if (g_progress_frame >= 0)
    {
        write_progress_line_locked(g_progress_frame);
    }
    end_progress_line_locked();
}

} // namespace vidwizard
