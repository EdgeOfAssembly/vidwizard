/**
 * @file parse_time_harness.c
 * @brief CBMC harness for timestamp / range / crop / speed parsers.
 */

#include "vidwizard/parse_time.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    double t = -1.0;
    vw_range ranges[4];
    size_t n = 0;
    vw_crop crop;
    char rbuf[64];
    double factor = 0.0;
    char name[32];

    assert(vw_parse_timestamp("10", &t) == 0);
    assert(t == 10.0);

    assert(vw_parse_timestamp("1:30", &t) == 0);
    assert(t == 90.0);

    assert(vw_parse_timestamp("1:00:00", &t) == 0);
    assert(t == 3600.0);

    assert(vw_parse_timestamp("abc", &t) != 0);
    assert(vw_parse_timestamp("1:60", &t) != 0);
    assert(vw_parse_timestamp("", &t) != 0);

    memset(ranges, 0, sizeof(ranges));
    assert(vw_parse_ranges("10-20,30-40", ranges, 4, &n) == 0);
    assert(n == 2);
    assert(ranges[0].start_s == 10.0);
    assert(ranges[0].end_s == 20.0);
    assert(ranges[1].start_s == 30.0);
    assert(vw_parse_ranges("20-10", ranges, 4, &n) != 0);

    memset(&crop, 0, sizeof(crop));
    rbuf[0] = '\0';
    assert(vw_parse_crop_spec("640x360+8+16", &crop, rbuf, sizeof(rbuf)) == 0);
    assert(crop.width == 640);
    assert(crop.height == 360);
    assert(crop.x == 8);
    assert(crop.y == 16);

    assert(vw_parse_speed_spec("2", &factor, rbuf, sizeof(rbuf)) == 0);
    assert(factor == 2.0);
    assert(vw_parse_speed_spec("0", &factor, rbuf, sizeof(rbuf)) != 0);

    assert(vw_frame_index_width(9) == 1);
    assert(vw_frame_index_width(10) == 2);
    assert(vw_frame_index_width(1000) == 4);

    assert(vw_format_frame_name(name, sizeof(name), "c", 4, 1) == 0);
    assert(strcmp(name, "c_0001.png") == 0);

    assert(vw_looks_like_ranges("10-20") == 1);
    assert(vw_looks_like_ranges("file.mp4") == 0);

    assert(vw_parse_ranges("10-", ranges, 4, &n) == 0);
    assert(n == 1);
    assert(ranges[0].start_s == 10.0);
    assert(ranges[0].end_s == VW_RANGE_UNTIL_EOF);
    vw_resolve_ranges(ranges, n, 40.0);
    assert(ranges[0].end_s == 40.0);

    assert(vw_parse_ranges("-5", ranges, 4, &n) == 0);
    assert(ranges[0].start_s == 0.0);
    assert(ranges[0].end_s == 5.0);

    return 0;
}
