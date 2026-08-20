#include "vidwizard/parse_time.h"
#include "vidwizard/version.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace fs = std::filesystem;

static std::string bin_path()
{
    if (const char *e = std::getenv("VIDWIZARD_BIN"))
    {
        return e;
    }
    if (fs::exists("./vidwizard"))
    {
        return "./vidwizard";
    }
    if (fs::exists("bin/debug/vidwizard"))
    {
        return "bin/debug/vidwizard";
    }
    return "vidwizard";
}

static int run_cmd(const std::string &cmd, std::string *out = nullptr)
{
    std::array<char, 256> buf{};
    std::string collected;
    FILE *fp = popen((cmd + " 2>&1").c_str(), "r");
    if (fp == nullptr)
    {
        return 127;
    }
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), fp) != nullptr)
    {
        collected += buf.data();
    }
    const int st = pclose(fp);
    if (out != nullptr)
    {
        *out = collected;
    }
    if (st == -1)
    {
        return 127;
    }
    return WEXITSTATUS(st);
}

static bool png_is_rgba8(const fs::path &p)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
    {
        return false;
    }
    unsigned char hdr[26]{};
    in.read(reinterpret_cast<char *>(hdr), sizeof(hdr));
    if (in.gcount() < 26)
    {
        return false;
    }
    static const unsigned char sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(hdr, sig, 8) != 0)
    {
        return false;
    }
    const unsigned char bit_depth = hdr[24];
    const unsigned char color_type = hdr[25];
    return bit_depth == 8 && color_type == 6;
}

TEST_CASE("CLI binary no-args help version", "[cli][contract]")
{
    const std::string b = bin_path();
    std::string out;
    REQUIRE(run_cmd(b, &out) == 0);
    REQUIRE(out.find("Usage: vidwizard") != std::string::npos);

    REQUIRE(run_cmd(b + " -h", &out) == 0);
    REQUIRE(out.find("--grayscale") != std::string::npos);

    REQUIRE(run_cmd(b + " --help", &out) == 0);
    REQUIRE(run_cmd(b + " -v", &out) == 0);
    REQUIRE(out.find(VIDWIZARD_VERSION) != std::string::npos);
    REQUIRE(run_cmd(b + " --version", &out) == 0);
}

TEST_CASE("CLI binary unknown option is non-zero stderr", "[cli][contract]")
{
    const std::string b = bin_path();
    std::string out;
    REQUIRE(run_cmd(b + " --definitely-not-a-flag x.mp4", &out) != 0);
    REQUIRE(out.find("unknown option") != std::string::npos);
    REQUIRE(out.find("Try 'vidwizard --help'") != std::string::npos);
    REQUIRE(out.find("Usage: vidwizard") == std::string::npos);
}

TEST_CASE("CLI binary --cut does not treat -o as ranges", "[cli][contract]")
{
    const std::string b = bin_path();
    std::string out;
    REQUIRE(run_cmd(b + " --cut -o x", &out) != 0);
    REQUIRE(out.find("requires a value") != std::string::npos);
    REQUIRE(out.find("Try 'vidwizard --help'") != std::string::npos);
    REQUIRE(out.find("invalid cut ranges") == std::string::npos);
    REQUIRE(out.find("Usage: vidwizard") == std::string::npos);
}

TEST_CASE("integration grayscale explode cut speed crop reverse mute", "[integration]")
{
    const fs::path clip = "testdata/clip.mp4";
    const fs::path red = "testdata/red.mp4";
    if (!fs::exists(clip) || !fs::exists(red))
    {
        SKIP("testdata fixtures missing (run make testdata)");
    }

    const std::string b = bin_path();
    const fs::path outdir = "testdata/out";
    fs::remove_all(outdir);
    fs::create_directories(outdir);

    std::string log;
    REQUIRE(run_cmd(b + " --grayscale " + clip.string() + " -o " + (outdir / "gray.mp4").string(),
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "gray.mp4"));

    REQUIRE(run_cmd(b + " --explode " + clip.string() + " -o " + (outdir / "frames").string() + "/",
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "frames" / "clip_01.png"));
    REQUIRE(fs::exists(outdir / "frames" / "clip_20.png"));
    REQUIRE_FALSE(fs::exists(outdir / "frames" / "clip_21.png"));
    REQUIRE(png_is_rgba8(outdir / "frames" / "clip_01.png"));

    REQUIRE(run_cmd(b + " --cut 0.5-1.5,1.5-2.0 " + clip.string() + " -o " + outdir.string() + "/",
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "clip_cut_1.mp4"));
    REQUIRE(fs::exists(outdir / "clip_cut_2.mp4"));

    REQUIRE(run_cmd(b + " --speed 2 " + clip.string() + " -o " + (outdir / "fast.mp4").string(),
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "fast.mp4"));

    REQUIRE(run_cmd(b + " --crop 160x120+0+0 " + clip.string() + " -o " +
                        (outdir / "crop.mp4").string(),
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "crop.mp4"));

    REQUIRE(run_cmd(b + " --reverse " + clip.string() + " -o " + (outdir / "rev.mp4").string(),
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "rev.mp4"));

    REQUIRE(run_cmd(b + " --mute " + clip.string() + " -o " + (outdir / "mute.mp4").string(),
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "mute.mp4"));

    REQUIRE(run_cmd(b + " --grayscale --reverse " + red.string() + " -o " +
                        (outdir / "edit.mp4").string(),
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "edit.mp4"));

    /* Order independence on the binary. */
    REQUIRE(run_cmd(b + " " + clip.string() + " --grayscale -o " +
                        (outdir / "gray2.mp4").string(),
                    &log) == 0);

    /* Ranged grayscale. */
    REQUIRE(run_cmd(b + " --grayscale 0.2-0.8 " + clip.string() + " -o " +
                        (outdir / "gray_part.mp4").string(),
                    &log) == 0);

    REQUIRE(run_cmd(b + " --grayscale 0.5- " + clip.string() + " -o " +
                        (outdir / "gray_open.mp4").string(),
                    &log) == 0);
    REQUIRE(fs::exists(outdir / "gray_open.mp4"));
}

TEST_CASE("stereo speed keeps pitch-preserving audio", "[integration]")
{
    const fs::path stereo = "testdata/stereo.mp4";
    if (!fs::exists(stereo))
    {
        SKIP("testdata/stereo.mp4 missing");
    }
    const std::string b = bin_path();
    const fs::path out = "testdata/out/stereo_speed.mp4";
    fs::create_directories(out.parent_path());
    std::string log;
    REQUIRE(run_cmd(b + " --speed 2 " + stereo.string() + " -o " + out.string(), &log) == 0);
    REQUIRE(fs::exists(out));
    std::string probe;
    REQUIRE(run_cmd("ffprobe -v error -select_streams a:0 -show_entries stream=codec_type,channels "
                    "-of csv=p=0 " +
                        out.string(),
                    &probe) == 0);
    REQUIRE(probe.find("audio") != std::string::npos);
    REQUIRE(probe.find("2") != std::string::npos);
}

TEST_CASE("past-EOF cut fails and does not leave a stub", "[integration]")
{
    const fs::path clip = "testdata/clip.mp4";
    if (!fs::exists(clip))
    {
        SKIP("testdata fixtures missing (run make testdata)");
    }

    const std::string b = bin_path();
    const fs::path outdir = "testdata/out";
    fs::create_directories(outdir);
    const fs::path out = outdir / "past_eof.mp4";
    fs::remove(out);

    std::string log;
    REQUIRE(run_cmd(b + " --cut 50-60 " + clip.string() + " -o " + out.string(), &log) != 0);
    REQUIRE(log.find("no frames") != std::string::npos);
    REQUIRE(log.find("past end") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}

TEST_CASE("explicit webm output is an error and leaves no stub", "[integration]")
{
    const fs::path clip = "testdata/clip.mp4";
    if (!fs::exists(clip))
    {
        SKIP("testdata fixtures missing (run make testdata)");
    }

    const std::string b = bin_path();
    const fs::path outdir = "testdata/out";
    fs::create_directories(outdir);
    const fs::path out = outdir / "explicit.webm";
    fs::remove(out);

    std::string log;
    REQUIRE(run_cmd(b + " --grayscale " + clip.string() + " -o " + out.string(), &log) != 0);
    REQUIRE(log.find("cannot write libx264 to WebM") != std::string::npos);
    REQUIRE_FALSE(fs::exists(out));
}

TEST_CASE("explode and cut both ranged is an error", "[integration]")
{
    const fs::path clip = "testdata/clip.mp4";
    if (!fs::exists(clip))
    {
        SKIP("testdata fixtures missing (run make testdata)");
    }

    const std::string b = bin_path();
    const fs::path outdir = "testdata/out/both_ranged";
    fs::remove_all(outdir);

    std::string log;
    REQUIRE(run_cmd(b + " --explode 0-1 --cut 0-1 " + clip.string() + " -o " + outdir.string() + "/",
                    &log) != 0);
    REQUIRE(log.find("cannot combine") != std::string::npos);
    REQUIRE_FALSE(fs::exists(outdir));
}

TEST_CASE("ffprobe crop size and mute has no audio", "[integration]")
{
    const fs::path crop = "testdata/out/crop.mp4";
    const fs::path mute = "testdata/out/mute.mp4";
    if (!fs::exists(crop) || !fs::exists(mute))
    {
        SKIP("previous integration outputs missing");
    }
    std::string out;
    REQUIRE(run_cmd("ffprobe -v error -select_streams v:0 -show_entries stream=width,height "
                    "-of csv=p=0 testdata/out/crop.mp4",
                    &out) == 0);
    REQUIRE(out.find("160,120") != std::string::npos);

    REQUIRE(run_cmd("ffprobe -v error -select_streams a:0 -show_entries stream=codec_type "
                    "-of csv=p=0 testdata/out/mute.mp4",
                    &out) == 0);
    REQUIRE(out.find("audio") == std::string::npos);
}
