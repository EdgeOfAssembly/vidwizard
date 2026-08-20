/**
 * @file cli.hpp
 * @brief Order-independent argv parser and usage text for vidwizard.
 */
#pragma once

#include "vidwizard/parse_time.h"
#include "vidwizard/version.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vidwizard
{

/**
 * @brief Parsed command line.
 *
 * Empty range vectors mean “the whole timeline” for that operation.
 */
struct cli_options
{
    bool help = false;
    bool version = false;
    bool verbose = false;
    std::optional<std::filesystem::path> output;
    std::optional<std::filesystem::path> log_file;
    unsigned jobs = 0; /**< 0 = autodetect logical cores. */

    bool grayscale = false;
    std::vector<vw_range> grayscale_ranges;

    bool explode = false;
    std::vector<vw_range> explode_ranges;

    bool cut = false;
    std::vector<vw_range> cut_ranges;

    std::optional<double> speed_factor;
    std::vector<vw_range> speed_ranges;

    std::optional<vw_crop> crop;
    std::vector<vw_range> crop_ranges;

    bool reverse = false;
    std::vector<vw_range> reverse_ranges;

    bool mute = false;
    std::vector<vw_range> mute_ranges;

    std::vector<vw_zoom_seg> zoom;

    std::vector<vw_text> texts;
    std::string text_font;
    std::string text_style{"bold"};
    int text_size = 28;
    std::string text_color{"#ffffff"};
    std::string text_bg; /**< Empty = transparent (no box). */

    std::vector<std::filesystem::path> inputs;
};

/**
 * @brief Result of parsing argv.
 *
 * @p early_exit is set for help/version (and no-args usage) so main can
 * print and return @p exit_code without processing files.
 */
struct parse_result
{
    cli_options opt{};
    std::string error;
    int exit_code = 0;
    bool early_exit = false;
};

/**
 * @brief Usage / help text (also printed when invoked with no arguments).
 *
 * @return Pointer to a static NUL-terminated usage string.
 */
const char *usage_text(void);

/**
 * @brief Parse argc/argv. Options and inputs may be interleaved.
 *
 * @param[in] argc Argument count (including argv[0]).
 * @param[in] argv Argument vector; must not be NULL.
 *
 * @return Parsed options, or @p error / @p early_exit set.
 */
parse_result parse_argv(int argc, char **argv);

/**
 * @brief True if at least one editing operation was requested.
 *
 * @param[in] opt Parsed options.
 *
 * @return true when grayscale/explode/cut/speed/crop/reverse/mute/zoom is set.
 */
bool has_operation(const cli_options &opt);

/**
 * @brief Count distinct enabled operations (for default output suffix).
 *
 * @param[in] opt Parsed options.
 *
 * @return Number of enabled ops in 0..7.
 */
int operation_count(const cli_options &opt);

/**
 * @brief Default output suffix (`_gray`, `_edit`, …) without extension.
 *
 * @param[in] opt Parsed options.
 *
 * @return Suffix string including the leading underscore, or empty.
 */
std::string default_suffix(const cli_options &opt);

/**
 * @brief Fill open-ended ranges (`10-`) from probed media duration.
 *
 * @param[in,out] opt Parsed options whose range vectors are updated in place.
 * @param[in]     duration_s Duration in seconds (0 → large fallback).
 */
void resolve_option_ranges(cli_options &opt, double duration_s);

} // namespace vidwizard
