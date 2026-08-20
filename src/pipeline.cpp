/**
 * @file pipeline.cpp
 * @brief Orchestrate explode / cut / transcode over expanded inputs.
 */

#include "vidwizard/pipeline.hpp"

#include "vidwizard/log.hpp"
#include "vidwizard/media.hpp"
#include "vidwizard/paths.hpp"
#include "vidwizard/threads.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <system_error>

namespace vidwizard
{

int process_one(const cli_options &opt, const std::filesystem::path &input,
                std::size_t input_count)
{
    const unsigned jobs = resolve_jobs(opt.jobs);

    if (opt.explode)
    {
        cli_options local = opt;
        if (local.cut && local.explode_ranges.empty())
        {
            local.explode_ranges = local.cut_ranges;
        }
        const std::filesystem::path prefix = explode_prefix(input, opt.output, input_count);
        log_error("vidwizard: explode %s → %s_*.png (%u threads)", input.c_str(), prefix.c_str(),
                  jobs);
        return explode_file(input, prefix, local, jobs);
    }

    if (opt.cut)
    {
        const unsigned n = static_cast<unsigned>(opt.cut_ranges.size());
        if (n == 0U)
        {
            log_error("vidwizard: --cut requires at least one START-END range");
            return 1;
        }
        const std::string suffix = default_suffix(opt);
        const unsigned inner = std::max(1U, jobs / n);
        thread_pool pool(std::min(jobs, n));
        std::atomic<int> errors{0};

        for (unsigned i = 0; i < n; i++)
        {
            const vw_range range = opt.cut_ranges[i];
            const std::filesystem::path out =
                cut_output_path(input, opt.output, input_count, i + 1U, n, suffix);
            pool.submit([input, out, opt, range, inner, &errors]() {
                cli_options local = opt;
                local.cut = false;
                local.jobs = inner;
                time_window window{};
                window.enabled = true;
                window.start_s = range.start_s;
                window.end_s = range.end_s;
                if (range.end_s < 0.0)
                {
                    log_error("vidwizard: cut %s [%.3f-EOF) → %s", input.c_str(), range.start_s,
                              out.c_str());
                }
                else
                {
                    log_error("vidwizard: cut %s [%.3f-%.3f) → %s", input.c_str(), range.start_s,
                              range.end_s, out.c_str());
                }
                if (transcode_file(input, out, local, window, inner) != 0)
                {
                    errors.fetch_add(1);
                }
            });
        }
        pool.wait_idle();
        return errors.load() == 0 ? 0 : 2;
    }

    const std::filesystem::path out = default_video_output(input, opt, opt.output, input_count);
    log_error("vidwizard: %s → %s (%u threads)", input.c_str(), out.c_str(), jobs);
    return transcode_file(input, out, opt, time_window{}, jobs);
}

int run_pipeline(const cli_options &opt)
{
    const std::vector<std::filesystem::path> files = expand_inputs(opt.inputs);
    if (files.empty())
    {
        log_error("vidwizard: no input video files");
        return 1;
    }
    if (!has_operation(opt))
    {
        log_error("vidwizard: specify an operation (--grayscale, --explode, --cut, --speed, "
                  "--crop, --reverse, --mute)");
        return 1;
    }

    int errors = 0;
    for (const std::filesystem::path &in : files)
    {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(in, ec))
        {
            log_error("vidwizard: not a file: %s", in.c_str());
            errors++;
            continue;
        }
        const int rc = process_one(opt, in, files.size());
        if (rc != 0)
        {
            errors++;
        }
    }
    return errors == 0 ? 0 : 2;
}

} // namespace vidwizard
