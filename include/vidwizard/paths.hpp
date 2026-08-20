/**
 * @file paths.hpp
 * @brief Input directory expansion and default output naming.
 */
#pragma once

#include "vidwizard/cli.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vidwizard
{

/**
 * @brief True if @p p has a known video file extension.
 *
 * @param[in] p Path to test.
 *
 * @return true for mp4/mkv/webm/mov/avi and other common containers.
 */
bool is_video_file(const std::filesystem::path &p);

/**
 * @brief Expand files and directories (non-recursive) into video paths.
 *
 * Directories contribute their direct children that pass is_video_file().
 * Hidden names (leading `.`) are skipped. Result is sorted by generic string.
 *
 * @param[in] inputs Operand paths from the CLI.
 *
 * @return Expanded file list (may be empty).
 */
std::vector<std::filesystem::path> expand_inputs(
    const std::vector<std::filesystem::path> &inputs);

/**
 * @brief True if `-o` should be treated as a directory target.
 *
 * @param[in] output Requested -o path.
 * @param[in] input_count Number of expanded inputs.
 * @param[in] explode PNG sequence mode (directory-oriented).
 *
 * @return true when outputs should be written inside @p output.
 */
bool output_is_directory(const std::filesystem::path &output, std::size_t input_count,
                         bool explode);

/**
 * @brief Default video output path for one input (no explode, no multi-cut).
 *
 * @param[in] input Input file.
 * @param[in] opt   Parsed options (suffix).
 * @param[in] output_opt Optional -o file or directory.
 * @param[in] input_count Expanded input count (multi-in -o file rule).
 *
 * @return Destination path.
 */
std::filesystem::path default_video_output(const std::filesystem::path &input,
                                           const cli_options &opt,
                                           const std::optional<std::filesystem::path> &output_opt,
                                           std::size_t input_count);

/**
 * @brief Prefix used for `prefix_0001.png` (no trailing underscore).
 *
 * @param[in] input Input file.
 * @param[in] output_opt Optional -o file, directory, or prefix.
 * @param[in] input_count Expanded input count.
 *
 * @return Prefix path without the `_NNNN.png` suffix.
 */
std::filesystem::path explode_prefix(const std::filesystem::path &input,
                                     const std::optional<std::filesystem::path> &output_opt,
                                     std::size_t input_count);

/**
 * @brief Path for one `--cut` subvideo (`stem_cut_01.ext`).
 *
 * @param[in] input Input file.
 * @param[in] output_opt Optional -o.
 * @param[in] input_count Expanded input count.
 * @param[in] index 1-based cut index.
 * @param[in] cut_count Total cuts (pad width).
 * @param[in] suffix Extra suffix before the index (`_cut` or `_edit_cut`).
 *
 * @return Destination path.
 */
std::filesystem::path cut_output_path(const std::filesystem::path &input,
                                      const std::optional<std::filesystem::path> &output_opt,
                                      std::size_t input_count, unsigned index,
                                      unsigned cut_count, const std::string &suffix);

/**
 * @brief Create parent directories of @p path if needed.
 *
 * @param[in] path File path whose parent should exist.
 *
 * @return 0 on success, -1 on failure.
 */
int ensure_parent_directory(const std::filesystem::path &path);

} // namespace vidwizard
