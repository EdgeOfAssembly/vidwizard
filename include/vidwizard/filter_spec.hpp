/**
 * @file filter_spec.hpp
 * @brief Build libavfilter graph strings from CLI operations.
 */
#pragma once

#include "vidwizard/cli.hpp"

#include <string>
#include <utility>
#include <vector>

namespace vidwizard
{

/**
 * @brief Optional time window for `--cut` (original-timeline seconds).
 */
struct time_window
{
    bool enabled = false;
    double start_s = 0.0;
    double end_s = 0.0;
};

/**
 * @brief Video/audio filtergraph pair. Empty audio means “drop audio”.
 */
struct filter_graphs
{
    std::string video; /**< libavfilter chain or `null`. */
    std::string audio; /**< libavfilter chain, `anull`, or empty to drop. */
    bool drop_audio = false;
    bool uses_split = false; /**< Segment concat graph (ranged reverse/speed/crop). */
};

/**
 * @brief Chain of `atempo` filters covering @p factor (each step in [0.5, 2]).
 *
 * @param[in] factor Speed multiplier; must be > 0.
 *
 * @return Filter string such as `atempo=2,atempo=1.375`.
 */
std::string atempo_chain(double factor);

/**
 * @brief FFmpeg `enable=` expression for @p ranges, or empty if whole timeline.
 *
 * @param[in] ranges Time windows; empty → no enable clause.
 *
 * @return `between(t,a,b)+…` or empty.
 */
std::string enable_expression(const std::vector<vw_range> &ranges);

/**
 * @brief Rec.709 luma mix (high-quality grayscale) with optional enable.
 *
 * @param[in] ranges Empty means whole frame stream.
 *
 * @return Filter substring (no leading/trailing commas).
 */
std::string grayscale_filter(const std::vector<vw_range> &ranges);

/**
 * @brief Build video and audio graphs for one output.
 *
 * @param[in] opt     CLI operations.
 * @param[in] window  Optional cut window (frames outside are not fed).
 * @param[in] src_w   Source width (for ranged crop scale-back).
 * @param[in] src_h   Source height.
 * @param[in] duration_s Known duration (0 if unknown).
 * @param[in] crop_resolved Crop with x/y filled (centered already applied).
 * @param[in] fps_num Source frames-per-second numerator (0 if unknown).
 * @param[in] fps_den Source frames-per-second denominator.
 *
 * @return Filter strings ready for avfilter_graph_parse_ptr.
 */
filter_graphs build_filter_graphs(const cli_options &opt, const time_window &window,
                                  int src_w, int src_h, double duration_s,
                                  const vw_crop *crop_resolved, int fps_num, int fps_den);

/**
 * @brief True when reverse/speed/crop ranges require a split+concat graph.
 *
 * @param[in] opt CLI operations.
 *
 * @return true if a segment graph is needed.
 */
bool needs_segment_graph(const cli_options &opt);

} // namespace vidwizard
