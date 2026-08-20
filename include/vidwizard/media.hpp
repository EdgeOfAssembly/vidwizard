/**
 * @file media.hpp
 * @brief FFmpeg transcode and PNG explode entry points.
 */
#pragma once

#include "vidwizard/cli.hpp"
#include "vidwizard/filter_spec.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace vidwizard
{

/**
 * @brief Human-readable libav error string.
 *
 * @param[in] err Negative AVERROR code.
 *
 * @return Message suitable for stderr.
 */
std::string av_error_string(int err);

/**
 * @brief Decode/filter/encode one output video.
 *
 * @param[in] input  Source file.
 * @param[in] output Destination file (parent dir created).
 * @param[in] opt    Operations (cut flag ignored; use @p window).
 * @param[in] window Optional original-timeline window.
 * @param[in] jobs   Encoder/filter thread count (>= 1).
 *
 * @return 0 on success, non-zero on failure.
 */
int transcode_file(const std::filesystem::path &input, const std::filesystem::path &output,
                   const cli_options &opt, const time_window &window, unsigned jobs);

/**
 * @brief Export filtered frames as max-compressed 32-bit RGBA PNG files.
 *
 * @param[in] input  Source file.
 * @param[in] prefix Path prefix (writes `prefix_0001.png`, …).
 * @param[in] opt    Operations (explode ranges, grayscale, crop, reverse).
 * @param[in] jobs   PNG worker threads (>= 1).
 *
 * @return 0 on success, non-zero on failure.
 */
int explode_file(const std::filesystem::path &input, const std::filesystem::path &prefix,
                 const cli_options &opt, unsigned jobs);

/**
 * @brief Fill centered crop x/y and even-align for YUV 4:2:0.
 *
 * @param[in,out] crop Geometry to resolve.
 * @param[in]     src_w Source width.
 * @param[in]     src_h Source height.
 *
 * @return 0 on success, -1 if the rectangle does not fit.
 */
int resolve_crop(vw_crop *crop, int src_w, int src_h);

} // namespace vidwizard
