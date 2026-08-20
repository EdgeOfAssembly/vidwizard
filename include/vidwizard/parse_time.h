/**
 * @file parse_time.h
 * @brief Time-range, crop-geometry, and speed-token parsers (C23, CBMC-friendly).
 */
#ifndef VIDWIZARD_PARSE_TIME_H
#define VIDWIZARD_PARSE_TIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of START-END pairs accepted in one list. */
#define VW_MAX_RANGES 64

/** Maximum animated zoom segments in one `--zoom` spec. */
#define VW_MAX_ZOOMS 16

/** Maximum characters in one `--text` string (excluding NUL). */
#define VW_MAX_TEXT 255

/** Maximum characters in a copied ranges substring (including NUL). */
#define VW_MAX_RANGES_CHARS 512

/**
 * @brief Sentinel for an open end (`10-`): until EOF, resolved later.
 */
#define VW_RANGE_UNTIL_EOF (-1.0)

/**
 * @brief Half-open time window in seconds from the start of the media.
 *
 * End is exclusive in the pipeline (`start_s <= t < end_s`).
 * @p end_s equal to #VW_RANGE_UNTIL_EOF means “until the clip ends”
 * (`10-`, `1:00-`). Call vw_resolve_ranges() once duration is known.
 */
typedef struct vw_range
{
    double start_s;
    double end_s;
} vw_range;

/**
 * @brief Pixel crop rectangle.
 *
 * When @p centered is non-zero, @p x and @p y are ignored until the
 * pipeline fills them from the source frame size.
 */
typedef struct vw_crop
{
    int width;
    int height;
    int x;
    int y;
    int centered;
} vw_crop;

/**
 * @brief One zoom keyframe: animate z0→z1 over @p range at normalized center.
 *
 * @p z0 == @p z1 holds a constant factor. Center @p cx,@p cy are 0..1
 * (0.5,0.5 is frame centre). @p range.end_s may be #VW_RANGE_UNTIL_EOF.
 */
typedef struct vw_zoom_seg
{
    double z0;
    double z1;
    double cx;
    double cy;
    vw_range range;
} vw_zoom_seg;

/**
 * @brief Parse a timestamp into seconds.
 *
 * Accepts `S`, `S.frac`, `M:SS`, `M:SS.frac`, `H:MM:SS`, `H:MM:SS.frac`.
 * Leading and trailing spaces are ignored. Seconds (and minutes in the
 * three-field form) must be strictly less than 60.
 *
 * @param[in]  s            NUL-terminated input; must not be NULL.
 * @param[out] out_seconds  Result in seconds; only written on success.
 *
 * @retval  0        Success.
 * @retval -22       EINVAL: NULL, empty, trailing junk, or field out of range.
 *
 * @note Hours may exceed 59 (`90:00` is ninety minutes).
 */
int vw_parse_timestamp(const char *s, double *out_seconds);

/**
 * @brief Parse `START-END,START-END,…` into @p buf.
 *
 * @param[in]  s      NUL-terminated list; must not be NULL.
 * @param[out] buf    Output array; must not be NULL.
 * @param[in]  cap    Capacity of @p buf (elements).
 * @param[out] out_n  Number of ranges written; must not be NULL.
 *
 * Open ends: `10-` / `1:00-` (until EOF), `-20` (from 0).
 *
 * @retval  0   Success (at least one range).
 * @retval -22  EINVAL: syntax, start >= end, or NULL.
 * @retval -28  ENOSPC: more than @p cap ranges (or more than VW_MAX_RANGES).
 */
int vw_parse_ranges(const char *s, vw_range *buf, size_t cap, size_t *out_n);

/**
 * @brief Replace #VW_RANGE_UNTIL_EOF with @p duration_s (or a large fallback).
 *
 * @param[in,out] buf         Range array.
 * @param[in]     n           Number of ranges.
 * @param[in]     duration_s  Media duration in seconds; 0 → 1e12 fallback.
 */
void vw_resolve_ranges(vw_range *buf, size_t n, double duration_s);

/**
 * @brief Return 1 if @p s is a valid range list, else 0.
 *
 * Used by the CLI to decide whether the next argv token is ranges or a path.
 *
 * @param[in] s Candidate string; NULL is treated as not-a-range-list.
 *
 * @return 1 if @p s parses as one or more START-END pairs, otherwise 0.
 */
int vw_looks_like_ranges(const char *s);

/**
 * @brief Parse crop geometry, optionally followed by `:RANGES`.
 *
 * Geometry forms:
 * - `WxH+X+Y` (ImageMagick)
 * - `WxH` (centered)
 * - `W:H:X:Y` (FFmpeg)
 * - `W:H` (centered)
 *
 * @param[in]  s           NUL-terminated spec; must not be NULL.
 * @param[out] crop        Geometry; only written on success.
 * @param[out] ranges      Optional copied ranges string (empty if none).
 * @param[in]  ranges_cap  Capacity of @p ranges; ignored if @p ranges is NULL.
 *
 * @retval  0   Success.
 * @retval -22  EINVAL.
 * @retval -28  ENOSPC if ranges do not fit @p ranges_cap.
 */
int vw_parse_crop_spec(const char *s, vw_crop *crop, char *ranges, size_t ranges_cap);

/**
 * @brief Parse `FACTOR` or `FACTOR:RANGES`.
 *
 * @param[in]  s           NUL-terminated spec; must not be NULL.
 * @param[out] factor      Speed multiplier; must be finite and > 0.
 * @param[out] ranges      Optional copied ranges string (empty if none).
 * @param[in]  ranges_cap  Capacity of @p ranges; ignored if @p ranges is NULL.
 *
 * @retval  0   Success.
 * @retval -22  EINVAL (including non-positive factor).
 * @retval -28  ENOSPC.
 */
int vw_parse_speed_spec(const char *s, double *factor, char *ranges, size_t ranges_cap);

/**
 * @brief Parse `--zoom` spec: `Z0[-Z1][@CX,CY][:RANGE]` segments split by `;`.
 *
 * Examples: `2`, `2:3-5`, `1-2.5@0.5,0.4:3-5`,
 * `1-2.4@0.5,0.4:2-4;2.4-1:4-6`.
 *
 * @retval  0   Success.
 * @retval -22  EINVAL.
 * @retval -28  ENOSPC.
 */
int vw_parse_zoom_spec(const char *s, vw_zoom_seg *buf, size_t cap, size_t *out_n);

/**
 * @brief One `--text` overlay: string, pixel origin, optional time window.
 *
 * @p has_range is 0 when the label lasts the whole clip.
 */
typedef struct vw_text
{
    char text[VW_MAX_TEXT + 1];
    int x;
    int y;
    int has_range;
    vw_range range;
} vw_text;

/**
 * @brief Parse `TEXT[:RANGE][+X+Y]`.
 *
 * Range and position are optional and parsed from the right so the
 * label may contain colons. Default origin is 0,0 (top-left).
 *
 * @retval  0   Success.
 * @retval -22  EINVAL.
 */
int vw_parse_text_spec(const char *s, vw_text *out);

/**
 * @brief Parse `RRGGBB` or `RRGGBBAA` (optional leading `#`).
 *
 * @param[in]  s    Hex color; must not be NULL.
 * @param[out] out  Canonical `#RRGGBB` or `#RRGGBBAA`.
 * @param[in]  cap  Capacity of @p out.
 *
 * @retval  0   Success.
 * @retval -22  EINVAL.
 * @retval -28  ENOSPC.
 */
int vw_parse_hex_color(const char *s, char *out, size_t cap);

/**
 * @brief Decimal digit width needed to write 1..@p frame_count.
 *
 * @param[in] frame_count Number of frames that will be numbered (at least 1 used).
 *
 * @return Digit count (1 for 1..9, 2 for 10..99, …). Zero @p frame_count yields 1.
 */
int vw_frame_index_width(uint64_t frame_count);

/**
 * @brief True if time @p t_s is inside any range, or if @p n is 0 (whole media).
 *
 * Empty range list means “apply to the whole timeline”.
 *
 * @param[in] ranges Range array (may be NULL when @p n is 0).
 * @param[in] n      Number of ranges.
 * @param[in] t_s    Time in seconds.
 *
 * @return 1 if the effect applies at @p t_s, else 0.
 */
int vw_ranges_cover(const vw_range *ranges, size_t n, double t_s);

/**
 * @brief Format `prefix_0001.png` into @p dst.
 *
 * @param[out] dst     Destination buffer.
 * @param[in]  cap     Capacity of @p dst in bytes.
 * @param[in]  prefix  Path prefix (video stem); must not be NULL.
 * @param[in]  width   Zero-pad width (>= 1).
 * @param[in]  index   1-based frame index.
 *
 * @retval  0   Success.
 * @retval -22  EINVAL.
 * @retval -28  ENOSPC.
 */
int vw_format_frame_name(char *dst, size_t cap, const char *prefix, int width,
                         uint64_t index);

#ifdef __cplusplus
}
#endif

#endif /* VIDWIZARD_PARSE_TIME_H */
