#include "vidwizard/cli.hpp"
#include "vidwizard/paths.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

TEST_CASE("is_video_file by extension")
{
    REQUIRE(vidwizard::is_video_file("a.mp4"));
    REQUIRE(vidwizard::is_video_file("A.MKV"));
    REQUIRE(vidwizard::is_video_file("clip.webm"));
    REQUIRE_FALSE(vidwizard::is_video_file("readme.txt"));
    REQUIRE_FALSE(vidwizard::is_video_file("photo.png"));
}

TEST_CASE("default video output naming")
{
    vidwizard::cli_options opt{};
    opt.grayscale = true;
    const auto p = vidwizard::default_video_output("dir/clip.mp4", opt, std::nullopt, 1);
    REQUIRE(p.filename() == "clip_gray.mp4");

    const auto o = vidwizard::default_video_output("dir/clip.mp4", opt,
                                                   std::optional<std::filesystem::path>{"out.mp4"}, 1);
    REQUIRE(o == std::filesystem::path("out.mp4"));
}

TEST_CASE("explode prefix")
{
    const auto p = vidwizard::explode_prefix("dir/movie.mp4", std::nullopt, 1);
    REQUIRE(p.filename() == "movie");
}

TEST_CASE("cut output padded index")
{
    const auto a = vidwizard::cut_output_path("clip.mp4", std::nullopt, 1, 1, 2, "_cut");
    REQUIRE(a.filename() == "clip_cut_1.mp4");
    const auto b = vidwizard::cut_output_path("clip.mp4", std::nullopt, 1, 2, 12, "_cut");
    REQUIRE(b.filename() == "clip_cut_02.mp4");
}

TEST_CASE("default and cut outputs remap webm input to mp4")
{
    vidwizard::cli_options opt{};
    opt.grayscale = true;
    const auto p = vidwizard::default_video_output("dir/clip.webm", opt, std::nullopt, 1);
    REQUIRE(p.filename() == "clip_gray.mp4");
    REQUIRE(vidwizard::is_webm_path("out.webm"));
    REQUIRE(vidwizard::is_webm_path("OUT.WEBM"));
    REQUIRE_FALSE(vidwizard::is_webm_path("out.mp4"));

    const auto explicit_webm = vidwizard::default_video_output(
        "dir/clip.mp4", opt, std::optional<std::filesystem::path>{"out.webm"}, 1);
    REQUIRE(explicit_webm == std::filesystem::path("out.webm"));

    const auto cut = vidwizard::cut_output_path("clip.webm", std::nullopt, 1, 1, 1, "_cut");
    REQUIRE(cut.filename() == "clip_cut_1.mp4");
}
