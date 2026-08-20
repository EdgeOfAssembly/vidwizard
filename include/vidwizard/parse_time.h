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

/** Maximum characters in a copied ranges substring (including NUL). */
#define VW_MAX_RANGES_CHARS 512

/**
 * @brief Half-open time window in seconds from the start of the media.
 *
 * End is exclusive in the pipeline (`start_s <= t < end_s`).
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
 * @retval  0   Success (at least one range).
 * @retval -22  EINVAL: syntax, start >= end, or NULL.
 * @retval -28  ENOSPC: more than @p cap ranges (or more than VW_MAX_RANGES).
 */
int vw_parse_ranges(const char *s, vw_range *buf, size_t cap, size_t *out_n);

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
