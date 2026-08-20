#include "vidwizard/cli.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using vidwizard::parse_argv;
using vidwizard::parse_result;

static parse_result parse_words(std::vector<std::string> words)
{
    std::vector<char *> argv;
    argv.reserve(words.size());
    for (std::string &w : words)
    {
        argv.push_back(w.data());
    }
    return parse_argv(static_cast<int>(argv.size()), argv.data());
}

TEST_CASE("no arguments is usage / help")
{
    auto r = parse_words({"vidwizard"});
    REQUIRE(r.early_exit);
    REQUIRE(r.opt.help);
    REQUIRE(r.exit_code == 0);
}

TEST_CASE("help and version flags")
{
    auto h = parse_words({"vidwizard", "-h"});
    REQUIRE(h.opt.help);
    REQUIRE(h.early_exit);

    auto H = parse_words({"vidwizard", "--help"});
    REQUIRE(H.opt.help);

    auto v = parse_words({"vidwizard", "-v"});
    REQUIRE(v.opt.version);
    REQUIRE(v.early_exit);
    REQUIRE_FALSE(v.opt.verbose);

    auto V = parse_words({"vidwizard", "--version"});
    REQUIRE(V.opt.version);
}

TEST_CASE("options and inputs may be interleaved")
{
    auto a = parse_words({"vidwizard", "--grayscale", "a.mp4"});
    REQUIRE(a.opt.grayscale);
    REQUIRE(a.opt.inputs.size() == 1);
    REQUIRE(a.opt.inputs[0] == "a.mp4");

    auto b = parse_words({"vidwizard", "a.mp4", "--grayscale"});
    REQUIRE(b.opt.grayscale);
    REQUIRE(b.opt.inputs.size() == 1);

    auto c = parse_words({"vidwizard", "-o", "out.mp4", "--grayscale", "a.mp4"});
    REQUIRE(c.opt.output.has_value());
    REQUIRE(c.opt.output->generic_string() == "out.mp4");
    REQUIRE(c.opt.grayscale);

    auto d = parse_words({"vidwizard", "a.mp4", "-o", "out.mp4", "--grayscale"});
    REQUIRE(d.opt.output.has_value());
    REQUIRE(d.opt.grayscale);
    REQUIRE(d.opt.inputs[0] == "a.mp4");
}

TEST_CASE("range-valued operations")
{
    auto g = parse_words({"vidwizard", "--grayscale=10-20,30-40", "a.mp4"});
    REQUIRE(g.opt.grayscale);
    REQUIRE(g.opt.grayscale_ranges.size() == 2);

    auto g2 = parse_words({"vidwizard", "--grayscale", "10-20", "a.mp4"});
    REQUIRE(g2.opt.grayscale_ranges.size() == 1);
    REQUIRE(g2.opt.inputs.size() == 1);

    auto cut = parse_words({"vidwizard", "--cut", "1:00-1:30,2:00-2:10", "a.mp4"});
    REQUIRE(cut.opt.cut);
    REQUIRE(cut.opt.cut_ranges.size() == 2);

    auto sp = parse_words({"vidwizard", "--speed", "2.75", "a.mp4"});
    REQUIRE(sp.opt.speed_factor.has_value());
    REQUIRE(*sp.opt.speed_factor == Catch::Approx(2.75));

    auto sp2 = parse_words({"vidwizard", "--speed=2:10-20,30-40", "a.mp4"});
    REQUIRE(*sp2.opt.speed_factor == Catch::Approx(2.0));
    REQUIRE(sp2.opt.speed_ranges.size() == 2);

    auto crop = parse_words({"vidwizard", "--crop", "160x120+0+0", "a.mp4"});
    REQUIRE(crop.opt.crop.has_value());
    REQUIRE(crop.opt.crop->width == 160);

    auto mute = parse_words({"vidwizard", "--mute=0-1", "a.mp4"});
    REQUIRE(mute.opt.mute);
    REQUIRE(mute.opt.mute_ranges.size() == 1);
}

TEST_CASE("unknown option and missing values")
{
    auto u = parse_words({"vidwizard", "--not-a-flag", "a.mp4"});
    REQUIRE_FALSE(u.error.empty());
    REQUIRE(u.exit_code != 0);

    auto j = parse_words({"vidwizard", "--jobs", "0", "a.mp4"});
    REQUIRE_FALSE(j.error.empty());

    auto c = parse_words({"vidwizard", "--cut"});
    REQUIRE_FALSE(c.error.empty());
}

TEST_CASE("operation count and suffix")
{
    auto g = parse_words({"vidwizard", "--grayscale", "a.mp4"});
    REQUIRE(vidwizard::operation_count(g.opt) == 1);
    REQUIRE(vidwizard::default_suffix(g.opt) == "_gray");

    auto both = parse_words({"vidwizard", "--grayscale", "--reverse", "a.mp4"});
    REQUIRE(vidwizard::operation_count(both.opt) == 2);
    REQUIRE(vidwizard::default_suffix(both.opt) == "_edit");
}

TEST_CASE("usage text contains required interface")
{
    const char *u = vidwizard::usage_text();
    REQUIRE(std::string(u).find("Usage: vidwizard") != std::string::npos);
    REQUIRE(std::string(u).find("-h, --help") != std::string::npos);
    REQUIRE(std::string(u).find("-v, --version") != std::string::npos);
    REQUIRE(std::string(u).find("--verbose") != std::string::npos);
    REQUIRE(std::string(u).find("vidwizard 0.1") != std::string::npos);
}
