#include "vidwizard/filter_spec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>

TEST_CASE("atempo chain splits outside 0.5-2")
{
    REQUIRE(vidwizard::atempo_chain(1.5) == "atempo=1.5");
    REQUIRE(vidwizard::atempo_chain(2.0) == "atempo=2");
    const std::string f275 = vidwizard::atempo_chain(2.75);
    REQUIRE(f275.find("atempo=2") != std::string::npos);
    REQUIRE(f275.find("atempo=") != std::string::npos);
    REQUIRE(vidwizard::atempo_chain(4.0) == "atempo=2,atempo=2");
    REQUIRE(vidwizard::atempo_chain(0.25) == "atempo=0.5,atempo=0.5");
}

TEST_CASE("enable expression")
{
    vw_range r[2] = {{10.0, 20.0}, {30.0, 40.0}};
    const std::string e = vidwizard::enable_expression({r[0], r[1]});
    REQUIRE(e.find("between(t,") != std::string::npos);
    REQUIRE(e.find("+") != std::string::npos);
    REQUIRE(vidwizard::enable_expression({}).empty());
}

TEST_CASE("simple grayscale graph")
{
    vidwizard::cli_options opt{};
    opt.grayscale = true;
    const auto g = vidwizard::build_filter_graphs(opt, {}, 320, 240, 2.0, nullptr, 10, 1);
    REQUIRE(g.video.find("colorchannelmixer") != std::string::npos);
    REQUIRE_FALSE(g.drop_audio);
}

TEST_CASE("mute whole drops audio")
{
    vidwizard::cli_options opt{};
    opt.mute = true;
    const auto g = vidwizard::build_filter_graphs(opt, {}, 320, 240, 2.0, nullptr, 10, 1);
    REQUIRE(g.drop_audio);
}

TEST_CASE("ranged reverse uses split concat")
{
    vidwizard::cli_options opt{};
    opt.reverse = true;
    opt.reverse_ranges.push_back(vw_range{0.5, 1.5});
    REQUIRE(vidwizard::needs_segment_graph(opt));
    const auto g = vidwizard::build_filter_graphs(opt, {}, 320, 240, 2.0, nullptr, 10, 1);
    REQUIRE(g.uses_split);
    REQUIRE(g.video.find("concat=") != std::string::npos);
    REQUIRE(g.video.find("reverse") != std::string::npos);
}

TEST_CASE("ranged crop pads black not scale")
{
    vidwizard::cli_options opt{};
    vw_crop crop{};
    crop.width = 160;
    crop.height = 120;
    crop.x = 80;
    crop.y = 40;
    opt.crop = crop;
    opt.crop_ranges.push_back(vw_range{0.5, 1.5});
    const auto g = vidwizard::build_filter_graphs(opt, {}, 320, 240, 2.0, &crop, 10, 1);
    REQUIRE(g.video.find("crop=160:120:80:40") != std::string::npos);
    REQUIRE(g.video.find("pad=320:240:80:40:black") != std::string::npos);
    REQUIRE(g.video.find("scale=320:240") == std::string::npos);
}

TEST_CASE("text overlay uses drawtext")
{
    vidwizard::cli_options opt{};
    vw_text t{};
    std::snprintf(t.text, sizeof(t.text), "Hello");
    t.has_range = 1;
    t.range.start_s = 1.0;
    t.range.end_s = 2.0;
    opt.texts.push_back(t);
    const auto g = vidwizard::build_filter_graphs(opt, {}, 320, 240, 4.0, nullptr, 10, 1);
    REQUIRE(g.video.find("drawtext=") != std::string::npos);
    REQUIRE(g.video.find("Hello") != std::string::npos);
}

TEST_CASE("zoom filter uses zoompan")
{
    vidwizard::cli_options opt{};
    vw_zoom_seg z{};
    z.z0 = 1.0;
    z.z1 = 2.0;
    z.cx = 0.5;
    z.cy = 0.4;
    z.range.start_s = 2.0;
    z.range.end_s = 4.0;
    opt.zoom.push_back(z);
    const auto g = vidwizard::build_filter_graphs(opt, {}, 640, 360, 10.0, nullptr, 30, 1);
    REQUIRE(g.video.find("zoompan=") != std::string::npos);
    REQUIRE(g.video.find("d=1") != std::string::npos);
}

TEST_CASE("whole speed setpts and atempo")
{
    vidwizard::cli_options opt{};
    opt.speed_factor = 2.75;
    const auto g = vidwizard::build_filter_graphs(opt, {}, 320, 240, 2.0, nullptr, 10, 1);
    REQUIRE(g.video.find("setpts=PTS/") != std::string::npos);
    REQUIRE(g.video.find("fps=") != std::string::npos);
    REQUIRE(g.audio.find("atempo=") != std::string::npos);
    REQUIRE(g.audio.find("aformat=sample_fmts=fltp") != std::string::npos);
}
