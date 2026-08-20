/**
 * @file cli.cpp
 * @brief Argv parser: interleaved options/inputs, sane-default polarity.
 */

#include "vidwizard/cli.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace vidwizard
{
namespace
{

const char k_usage[] =
    "Usage: vidwizard [options] [inputs…]\n"
    "\n"
    "  inputs    Video files and/or directories (any order with options).\n"
    "            Directories expand to common video types (batch; no --batch flag).\n"
    "\n"
    "High-quality edits without a GUI. Quality and all logical CPU cores are\n"
    "the defaults. Time ranges: START-END,START-END,…  Times: seconds, MM:SS,\n"
    "or HH:MM:SS (fractions allowed).\n"
    "\n"
    "Options:\n"
    "  -h, --help                  Show this help and exit\n"
    "  -v, --version               Show version and exit\n"
    "  -o, --output PATH           Single output file or directory\n"
    "      --grayscale[=RANGES]    High-quality Rec.709 grayscale\n"
    "      --explode[=RANGES]      Lossless 32-bit RGBA PNG frames (max zip)\n"
    "      --cut RANGES            Extract each range as its own video\n"
    "      --speed FACTOR[:RANGES] Forward speed (e.g. 1.5, 2, 2.75)\n"
    "      --crop GEOM[:RANGES]    Crop WxH+X+Y or W:H:X:Y (WxH = centered)\n"
    "      --reverse[=RANGES]      Reverse whole video or ranges in place\n"
    "      --mute[=RANGES]         Drop all audio, or silence given ranges\n"
    "      --jobs N                Threads (default: all logical cores)\n"
    "      --verbose               Extra progress on stderr\n"
    "      --log-file PATH         Also write diagnostics to PATH\n"
    "\n"
    "Default outputs (no -o): <stem>_gray.ext, <stem>_01.png, <stem>_cut_01.ext,\n"
    "<stem>_speed.ext, <stem>_crop.ext, <stem>_rev.ext, <stem>_mute.ext, or\n"
    "<stem>_edit.ext when combining operations. PNG index width matches the\n"
    "frame count (20 frames → 01..20; 1000 frames → 0001..1000).\n"
    "\n"
    "vidwizard " VIDWIZARD_VERSION "\n";

bool starts_with(const char *s, const char *prefix)
{
    const std::size_t n = std::strlen(prefix);
    return std::strncmp(s, prefix, n) == 0;
}

int load_ranges(const char *text, std::vector<vw_range> *out, std::string *error,
                const char *what)
{
    vw_range buf[VW_MAX_RANGES];
    size_t n = 0;
    const int rc = vw_parse_ranges(text, buf, VW_MAX_RANGES, &n);
    if (rc != 0)
    {
        *error = std::string("invalid ") + what + " ranges: " + text;
        return -1;
    }
    out->assign(buf, buf + n);
    return 0;
}

const char *take_value(int *i, int argc, char **argv, const char *eq, std::string *error,
                       const char *flag)
{
    if (eq != nullptr && eq[0] != '\0')
    {
        return eq;
    }
    if (*i + 1 >= argc)
    {
        *error = std::string(flag) + " requires a value";
        return nullptr;
    }
    (*i)++;
    return argv[*i];
}

const char *take_optional_ranges(int *i, int argc, char **argv, const char *eq)
{
    if (eq != nullptr && eq[0] != '\0')
    {
        return eq;
    }
    if (*i + 1 < argc && vw_looks_like_ranges(argv[*i + 1]))
    {
        (*i)++;
        return argv[*i];
    }
    return nullptr;
}

} // namespace

const char *usage_text(void)
{
    return k_usage;
}

bool has_operation(const cli_options &opt)
{
    return opt.grayscale || opt.explode || opt.cut || opt.speed_factor.has_value() ||
           opt.crop.has_value() || opt.reverse || opt.mute;
}

int operation_count(const cli_options &opt)
{
    int n = 0;
    n += opt.grayscale ? 1 : 0;
    n += opt.explode ? 1 : 0;
    n += opt.cut ? 1 : 0;
    n += opt.speed_factor.has_value() ? 1 : 0;
    n += opt.crop.has_value() ? 1 : 0;
    n += opt.reverse ? 1 : 0;
    n += opt.mute ? 1 : 0;
    return n;
}

std::string default_suffix(const cli_options &opt)
{
    const int n = operation_count(opt);
    if (n > 1)
    {
        return "_edit";
    }
    if (opt.grayscale)
    {
        return "_gray";
    }
    if (opt.explode)
    {
        return "";
    }
    if (opt.cut)
    {
        return "_cut";
    }
    if (opt.speed_factor.has_value())
    {
        return "_speed";
    }
    if (opt.crop.has_value())
    {
        return "_crop";
    }
    if (opt.reverse)
    {
        return "_rev";
    }
    if (opt.mute)
    {
        return "_mute";
    }
    return "_edit";
}

parse_result parse_argv(int argc, char **argv)
{
    parse_result result{};

    if (argc <= 1)
    {
        result.opt.help = true;
        result.early_exit = true;
        result.exit_code = 0;
        return result;
    }

    bool end_of_opts = false;
    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        if (arg == nullptr)
        {
            result.error = "internal: null argument";
            result.exit_code = 1;
            return result;
        }

        if (!end_of_opts && std::strcmp(arg, "--") == 0)
        {
            end_of_opts = true;
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0))
        {
            result.opt.help = true;
            continue;
        }
        if (!end_of_opts && (std::strcmp(arg, "-v") == 0 || std::strcmp(arg, "--version") == 0))
        {
            result.opt.version = true;
            continue;
        }
        if (!end_of_opts && std::strcmp(arg, "--verbose") == 0)
        {
            result.opt.verbose = true;
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0 ||
                             starts_with(arg, "--output=") ||
                             (starts_with(arg, "-o") && std::strcmp(arg, "-o") != 0 &&
                              arg[2] != '\0' && arg[1] != '-')))
        {
            const char *val = nullptr;
            if (starts_with(arg, "--output="))
            {
                val = arg + std::strlen("--output=");
            }
            else if (starts_with(arg, "-o") && std::strcmp(arg, "-o") != 0 &&
                     std::strcmp(arg, "--output") != 0)
            {
                val = arg + 2;
            }
            else
            {
                val = take_value(&i, argc, argv, nullptr, &result.error, "-o/--output");
                if (val == nullptr)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            if (result.opt.output.has_value())
            {
                result.error = "-o/--output may be given only once";
                result.exit_code = 1;
                return result;
            }
            result.opt.output = std::filesystem::path(val);
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--log-file") == 0 || starts_with(arg, "--log-file=")))
        {
            const char *eq = starts_with(arg, "--log-file=") ? arg + std::strlen("--log-file=") : nullptr;
            const char *val = take_value(&i, argc, argv, eq, &result.error, "--log-file");
            if (val == nullptr)
            {
                result.exit_code = 1;
                return result;
            }
            result.opt.log_file = std::filesystem::path(val);
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--jobs") == 0 || starts_with(arg, "--jobs=")))
        {
            const char *eq = starts_with(arg, "--jobs=") ? arg + std::strlen("--jobs=") : nullptr;
            const char *val = take_value(&i, argc, argv, eq, &result.error, "--jobs");
            if (val == nullptr)
            {
                result.exit_code = 1;
                return result;
            }
            char *end = nullptr;
            const long n = std::strtol(val, &end, 10);
            if (end == val || *end != '\0' || n < 1 || n > 4096)
            {
                result.error = "--jobs must be an integer >= 1";
                result.exit_code = 1;
                return result;
            }
            result.opt.jobs = static_cast<unsigned>(n);
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--grayscale") == 0 || starts_with(arg, "--grayscale=")))
        {
            result.opt.grayscale = true;
            const char *eq = starts_with(arg, "--grayscale=") ? arg + std::strlen("--grayscale=") : nullptr;
            const char *ranges = take_optional_ranges(&i, argc, argv, eq);
            if (ranges != nullptr)
            {
                if (load_ranges(ranges, &result.opt.grayscale_ranges, &result.error, "grayscale") !=
                    0)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--explode") == 0 || starts_with(arg, "--explode=")))
        {
            result.opt.explode = true;
            const char *eq = starts_with(arg, "--explode=") ? arg + std::strlen("--explode=") : nullptr;
            const char *ranges = take_optional_ranges(&i, argc, argv, eq);
            if (ranges != nullptr)
            {
                if (load_ranges(ranges, &result.opt.explode_ranges, &result.error, "explode") != 0)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--reverse") == 0 || starts_with(arg, "--reverse=")))
        {
            result.opt.reverse = true;
            const char *eq = starts_with(arg, "--reverse=") ? arg + std::strlen("--reverse=") : nullptr;
            const char *ranges = take_optional_ranges(&i, argc, argv, eq);
            if (ranges != nullptr)
            {
                if (load_ranges(ranges, &result.opt.reverse_ranges, &result.error, "reverse") != 0)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--mute") == 0 || starts_with(arg, "--mute=")))
        {
            result.opt.mute = true;
            const char *eq = starts_with(arg, "--mute=") ? arg + std::strlen("--mute=") : nullptr;
            const char *ranges = take_optional_ranges(&i, argc, argv, eq);
            if (ranges != nullptr)
            {
                if (load_ranges(ranges, &result.opt.mute_ranges, &result.error, "mute") != 0)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--cut") == 0 || starts_with(arg, "--cut=")))
        {
            const char *eq = starts_with(arg, "--cut=") ? arg + std::strlen("--cut=") : nullptr;
            const char *val = take_value(&i, argc, argv, eq, &result.error, "--cut");
            if (val == nullptr)
            {
                result.exit_code = 1;
                return result;
            }
            result.opt.cut = true;
            if (load_ranges(val, &result.opt.cut_ranges, &result.error, "cut") != 0)
            {
                result.exit_code = 1;
                return result;
            }
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--speed") == 0 || starts_with(arg, "--speed=")))
        {
            const char *eq = starts_with(arg, "--speed=") ? arg + std::strlen("--speed=") : nullptr;
            const char *val = take_value(&i, argc, argv, eq, &result.error, "--speed");
            if (val == nullptr)
            {
                result.exit_code = 1;
                return result;
            }
            char range_buf[VW_MAX_RANGES_CHARS];
            range_buf[0] = '\0';
            double factor = 0.0;
            if (vw_parse_speed_spec(val, &factor, range_buf, sizeof(range_buf)) != 0)
            {
                result.error = std::string("invalid --speed spec: ") + val;
                result.exit_code = 1;
                return result;
            }
            result.opt.speed_factor = factor;
            if (range_buf[0] != '\0')
            {
                if (load_ranges(range_buf, &result.opt.speed_ranges, &result.error, "speed") != 0)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            else if (i + 1 < argc && vw_looks_like_ranges(argv[i + 1]))
            {
                i++;
                if (load_ranges(argv[i], &result.opt.speed_ranges, &result.error, "speed") != 0)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            continue;
        }

        if (!end_of_opts && (std::strcmp(arg, "--crop") == 0 || starts_with(arg, "--crop=")))
        {
            const char *eq = starts_with(arg, "--crop=") ? arg + std::strlen("--crop=") : nullptr;
            const char *val = take_value(&i, argc, argv, eq, &result.error, "--crop");
            if (val == nullptr)
            {
                result.exit_code = 1;
                return result;
            }
            char range_buf[VW_MAX_RANGES_CHARS];
            range_buf[0] = '\0';
            vw_crop crop{};
            if (vw_parse_crop_spec(val, &crop, range_buf, sizeof(range_buf)) != 0)
            {
                result.error = std::string("invalid --crop spec: ") + val;
                result.exit_code = 1;
                return result;
            }
            result.opt.crop = crop;
            if (range_buf[0] != '\0')
            {
                if (load_ranges(range_buf, &result.opt.crop_ranges, &result.error, "crop") != 0)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            else if (i + 1 < argc && vw_looks_like_ranges(argv[i + 1]))
            {
                i++;
                if (load_ranges(argv[i], &result.opt.crop_ranges, &result.error, "crop") != 0)
                {
                    result.exit_code = 1;
                    return result;
                }
            }
            continue;
        }

        if (!end_of_opts && arg[0] == '-' && arg[1] != '\0')
        {
            result.error = std::string("unknown option: ") + arg;
            result.exit_code = 1;
            return result;
        }

        result.opt.inputs.emplace_back(arg);
    }

    if (result.opt.help || result.opt.version)
    {
        result.early_exit = true;
        result.exit_code = 0;
        return result;
    }

    return result;
}

} // namespace vidwizard
