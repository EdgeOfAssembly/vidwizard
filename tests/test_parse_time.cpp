#include "vidwizard/parse_time.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>

static void expect_ts(const char *s, double want)
{
    double got = -1.0;
    REQUIRE(vw_parse_timestamp(s, &got) == 0);
    REQUIRE(std::fabs(got - want) < 1e-6);
}

TEST_CASE("parse timestamp seconds and clock forms")
{
    expect_ts("0", 0.0);
    expect_ts("10", 10.0);
    expect_ts("10.5", 10.5);
    expect_ts("1:30", 90.0);
    expect_ts("1:30.5", 90.5);
    expect_ts("01:02:03", 3723.0);
    expect_ts("1:00:00", 3600.0);
    expect_ts("  12  ", 12.0);
    expect_ts("90:00", 5400.0);
}

TEST_CASE("parse timestamp rejects junk")
{
    double t = 0.0;
    REQUIRE(vw_parse_timestamp("", &t) != 0);
    REQUIRE(vw_parse_timestamp("abc", &t) != 0);
    REQUIRE(vw_parse_timestamp("1:60", &t) != 0);
    REQUIRE(vw_parse_timestamp("1:60:00", &t) != 0);
    REQUIRE(vw_parse_timestamp("1:00:60", &t) != 0);
    REQUIRE(vw_parse_timestamp("-1", &t) != 0);
    REQUIRE(vw_parse_timestamp(nullptr, &t) != 0);
    REQUIRE(vw_parse_timestamp("10", nullptr) != 0);
}

TEST_CASE("parse ranges comma list")
{
    vw_range buf[8];
    size_t n = 0;
    REQUIRE(vw_parse_ranges("10-20", buf, 8, &n) == 0);
    REQUIRE(n == 1);
    REQUIRE(buf[0].start_s == 10.0);
    REQUIRE(buf[0].end_s == 20.0);

    REQUIRE(vw_parse_ranges("10-20,30-40", buf, 8, &n) == 0);
    REQUIRE(n == 2);
    REQUIRE(buf[1].start_s == 30.0);

    REQUIRE(vw_parse_ranges("1:00-1:30,2:00-2:05.5", buf, 8, &n) == 0);
    REQUIRE(n == 2);
    REQUIRE(std::fabs(buf[0].start_s - 60.0) < 1e-9);
    REQUIRE(std::fabs(buf[0].end_s - 90.0) < 1e-9);
    REQUIRE(std::fabs(buf[1].end_s - 125.5) < 1e-9);
}

TEST_CASE("parse ranges rejects empty inverted and trailing comma")
{
    vw_range buf[4];
    size_t n = 0;
    REQUIRE(vw_parse_ranges("20-10", buf, 4, &n) != 0);
    REQUIRE(vw_parse_ranges("10-10", buf, 4, &n) != 0);
    REQUIRE(vw_parse_ranges("10-20,", buf, 4, &n) != 0);
    REQUIRE(vw_parse_ranges("", buf, 4, &n) != 0);
    REQUIRE(vw_looks_like_ranges("clip.mp4") == 0);
    REQUIRE(vw_looks_like_ranges("10-20") == 1);
    REQUIRE(vw_looks_like_ranges("10-20,30-40") == 1);
}

TEST_CASE("crop geometry ImageMagick and ffmpeg forms")
{
    vw_crop c{};
    char ranges[128];

    REQUIRE(vw_parse_crop_spec("640x360+10+20", &c, ranges, sizeof(ranges)) == 0);
    REQUIRE(c.width == 640);
    REQUIRE(c.height == 360);
    REQUIRE(c.x == 10);
    REQUIRE(c.y == 20);
    REQUIRE(c.centered == 0);
    REQUIRE(ranges[0] == '\0');

    REQUIRE(vw_parse_crop_spec("320x240", &c, ranges, sizeof(ranges)) == 0);
    REQUIRE(c.centered != 0);

    REQUIRE(vw_parse_crop_spec("640:360:8:16", &c, ranges, sizeof(ranges)) == 0);
    REQUIRE(c.width == 640);
    REQUIRE(c.x == 8);
    REQUIRE(c.y == 16);

    REQUIRE(vw_parse_crop_spec("160x120+0+0:1-2,3-4", &c, ranges, sizeof(ranges)) == 0);
    REQUIRE(std::strcmp(ranges, "1-2,3-4") == 0);
}

TEST_CASE("speed spec factor and optional ranges")
{
    double f = 0.0;
    char ranges[128];
    REQUIRE(vw_parse_speed_spec("2.75", &f, ranges, sizeof(ranges)) == 0);
    REQUIRE(std::fabs(f - 2.75) < 1e-9);
    REQUIRE(ranges[0] == '\0');

    REQUIRE(vw_parse_speed_spec("2:10-20,30-40", &f, ranges, sizeof(ranges)) == 0);
    REQUIRE(std::fabs(f - 2.0) < 1e-9);
    REQUIRE(std::strcmp(ranges, "10-20,30-40") == 0);

    REQUIRE(vw_parse_speed_spec("0", &f, ranges, sizeof(ranges)) != 0);
    REQUIRE(vw_parse_speed_spec("-1", &f, ranges, sizeof(ranges)) != 0);
}

TEST_CASE("frame index width and name")
{
    REQUIRE(vw_frame_index_width(0) == 1);
    REQUIRE(vw_frame_index_width(9) == 1);
    REQUIRE(vw_frame_index_width(10) == 2);
    REQUIRE(vw_frame_index_width(20) == 2);
    REQUIRE(vw_frame_index_width(1000) == 4);

    char name[64];
    REQUIRE(vw_format_frame_name(name, sizeof(name), "clip", 4, 1) == 0);
    REQUIRE(std::strcmp(name, "clip_0001.png") == 0);
    REQUIRE(vw_format_frame_name(name, sizeof(name), "clip", 2, 20) == 0);
    REQUIRE(std::strcmp(name, "clip_20.png") == 0);
}

TEST_CASE("ranges cover whole timeline when empty")
{
    REQUIRE(vw_ranges_cover(nullptr, 0, 3.0) == 1);
    vw_range r{.start_s = 1.0, .end_s = 2.0};
    REQUIRE(vw_ranges_cover(&r, 1, 1.0) == 1);
    REQUIRE(vw_ranges_cover(&r, 1, 1.5) == 1);
    REQUIRE(vw_ranges_cover(&r, 1, 2.0) == 0);
    REQUIRE(vw_ranges_cover(&r, 1, 0.5) == 0);
}
