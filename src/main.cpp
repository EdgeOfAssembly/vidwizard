/**
 * @file main.cpp
 * @brief vidwizard CLI entry: usage, version, pipeline.
 */

#include "vidwizard/cli.hpp"
#include "vidwizard/log.hpp"
#include "vidwizard/pipeline.hpp"
#include "vidwizard/threads.hpp"
#include "vidwizard/version.hpp"

extern "C"
{
#include <libavutil/log.h>
}

#include <cstdio>

int main(int argc, char **argv)
{
    using namespace vidwizard;

    const parse_result parsed = parse_argv(argc, argv);
    if (!parsed.error.empty())
    {
        std::fprintf(stderr, "vidwizard: %s\n", parsed.error.c_str());
        std::fprintf(stderr, "Try 'vidwizard --help'.\n");
        return parsed.exit_code != 0 ? parsed.exit_code : 1;
    }

    if (parsed.early_exit)
    {
        if (parsed.opt.version && !parsed.opt.help)
        {
            std::printf("%s %s\n", VIDWIZARD_NAME, VIDWIZARD_VERSION);
            return 0;
        }
        std::printf("%s", usage_text());
        return 0;
    }

    if (parsed.opt.verbose)
    {
        set_verbose(true);
        av_log_set_level(AV_LOG_WARNING);
    }
    else
    {
        av_log_set_level(AV_LOG_ERROR);
    }

    if (parsed.opt.log_file.has_value())
    {
        if (set_log_file(*parsed.opt.log_file) != 0)
        {
            std::fprintf(stderr, "vidwizard: cannot open log file %s\n",
                         parsed.opt.log_file->c_str());
            return 1;
        }
    }

    cli_options opt = parsed.opt;
    opt.jobs = resolve_jobs(opt.jobs);
    log_verbose("vidwizard: using %u thread(s) (host has %u logical core(s))", opt.jobs,
                logical_core_count());

    const int rc = run_pipeline(opt);
    close_log_file();
    return rc;
}
