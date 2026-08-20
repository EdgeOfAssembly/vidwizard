/**
 * @file paths.cpp
 * @brief Directory batch expansion and converter-style output names.
 */

#include "vidwizard/paths.hpp"

#include "vidwizard/log.hpp"
#include "vidwizard/parse_time.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace vidwizard
{
namespace
{

std::string lower_ext(const std::filesystem::path &p)
{
    std::string e = p.extension().string();
    for (char &c : e)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return e;
}

bool has_trailing_sep(const std::filesystem::path &p)
{
    const std::string s = p.generic_string();
    return !s.empty() && (s.back() == '/' || s.back() == '\\');
}

std::filesystem::path with_suffix(const std::filesystem::path &input, const std::string &suffix)
{
    const std::filesystem::path parent = input.parent_path();
    const std::string stem = input.stem().string();
    const std::string ext = input.extension().string();
    return parent / (stem + suffix + ext);
}

} // namespace

bool is_video_file(const std::filesystem::path &p)
{
    static const char *const k_exts[] = {".mp4",  ".mkv",  ".webm", ".mov",  ".avi",  ".m4v",
                                         ".mpeg", ".mpg",  ".ts",   ".m2ts", ".mts",  ".wmv",
                                         ".flv",  ".ogv",  ".nut",  ".y4m",  ".3gp",  ".asf",
                                         ".vob",  ".m2v",  ".mxf",  nullptr};
    const std::string e = lower_ext(p);
    if (e.empty())
    {
        return false;
    }
    for (int i = 0; k_exts[i] != nullptr; i++)
    {
        if (e == k_exts[i])
        {
            return true;
        }
    }
    return false;
}

std::vector<std::filesystem::path> expand_inputs(
    const std::vector<std::filesystem::path> &inputs)
{
    std::vector<std::filesystem::path> out;
    for (const std::filesystem::path &in : inputs)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(in, ec))
        {
            for (const std::filesystem::directory_entry &ent :
                 std::filesystem::directory_iterator(in, ec))
            {
                if (ec)
                {
                    break;
                }
                const std::filesystem::path p = ent.path();
                const std::string name = p.filename().string();
                if (name.empty() || name[0] == '.')
                {
                    continue;
                }
                if (ent.is_regular_file(ec) && is_video_file(p))
                {
                    out.push_back(p);
                }
            }
            continue;
        }
        out.push_back(in);
    }
    std::sort(out.begin(), out.end(), [](const std::filesystem::path &a,
                                         const std::filesystem::path &b) {
        return a.generic_string() < b.generic_string();
    });
    return out;
}

bool output_is_directory(const std::filesystem::path &output, std::size_t input_count,
                         bool explode)
{
    std::error_code ec;
    if (std::filesystem::is_directory(output, ec))
    {
        return true;
    }
    if (has_trailing_sep(output))
    {
        return true;
    }
    if (explode && !output.has_extension())
    {
        return true;
    }
    if (input_count > 1 && !std::filesystem::exists(output, ec) && !output.has_extension())
    {
        return true;
    }
    return false;
}

std::filesystem::path default_video_output(const std::filesystem::path &input,
                                           const cli_options &opt,
                                           const std::optional<std::filesystem::path> &output_opt,
                                           std::size_t input_count)
{
    const std::string suffix = default_suffix(opt);
    if (!output_opt.has_value())
    {
        return with_suffix(input, suffix);
    }

    const std::filesystem::path &o = *output_opt;
    if (output_is_directory(o, input_count, false))
    {
        std::error_code ec;
        std::filesystem::create_directories(o, ec);
        return o / (input.stem().string() + suffix + input.extension().string());
    }

    if (input_count > 1)
    {
        log_error("vidwizard: warning: multiple inputs with -o file; using %s_<stem>%s",
                  o.stem().string().c_str(), o.extension().string().c_str());
        const std::filesystem::path parent = o.parent_path();
        return parent / (o.stem().string() + "_" + input.stem().string() + o.extension().string());
    }

    return o;
}

std::filesystem::path explode_prefix(const std::filesystem::path &input,
                                     const std::optional<std::filesystem::path> &output_opt,
                                     std::size_t input_count)
{
    if (!output_opt.has_value())
    {
        return input.parent_path() / input.stem();
    }

    const std::filesystem::path &o = *output_opt;
    if (output_is_directory(o, input_count, true))
    {
        std::error_code ec;
        std::filesystem::create_directories(o, ec);
        return o / input.stem();
    }

    if (input_count > 1)
    {
        log_error("vidwizard: warning: multiple inputs with -o file; using %s_<stem>_NNNN.png",
                  o.stem().string().c_str());
        const std::filesystem::path parent = o.parent_path().empty() ? std::filesystem::path(".")
                                                                     : o.parent_path();
        return parent / (o.stem().string() + "_" + input.stem().string());
    }

    const std::filesystem::path parent = o.parent_path();
    return parent / o.stem();
}

std::filesystem::path cut_output_path(const std::filesystem::path &input,
                                      const std::optional<std::filesystem::path> &output_opt,
                                      std::size_t input_count, unsigned index, unsigned cut_count,
                                      const std::string &suffix)
{
    const int width = vw_frame_index_width(cut_count == 0U ? 1ULL : static_cast<uint64_t>(cut_count));
    char idx[32];
    std::snprintf(idx, sizeof(idx), "%0*u", width, index);

    const std::string ext = input.extension().string();
    const std::string name = input.stem().string() + suffix + "_" + idx + ext;

    if (!output_opt.has_value())
    {
        return input.parent_path() / name;
    }

    const std::filesystem::path &o = *output_opt;
    if (output_is_directory(o, input_count, false) || cut_count > 1U)
    {
        if (output_is_directory(o, input_count, false))
        {
            std::error_code ec;
            std::filesystem::create_directories(o, ec);
            return o / name;
        }
        if (cut_count > 1U && input_count <= 1)
        {
            log_error("vidwizard: warning: multiple cuts with -o file; using %s_%s%s",
                      o.stem().string().c_str(), idx, o.extension().string().c_str());
            const std::filesystem::path parent = o.parent_path();
            return parent / (o.stem().string() + "_" + idx + o.extension().string());
        }
    }

    if (cut_count == 1U && input_count <= 1)
    {
        return o;
    }

    return o.parent_path() / name;
}

int ensure_parent_directory(const std::filesystem::path &path)
{
    const std::filesystem::path parent = path.parent_path();
    if (parent.empty())
    {
        return 0;
    }
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec)
    {
        log_error("vidwizard: cannot create directory %s: %s", parent.c_str(),
                  ec.message().c_str());
        return -1;
    }
    return 0;
}

} // namespace vidwizard
