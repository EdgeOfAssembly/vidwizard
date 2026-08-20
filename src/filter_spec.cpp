/**
 * @file filter_spec.cpp
 * @brief libavfilter string builder (grayscale, crop, speed, reverse, mute).
 */

#include "vidwizard/filter_spec.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace vidwizard
{
namespace
{

std::string fmt_t(double t)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", t);
    return buf;
}

std::string fmt_f(double t)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.8g", t);
    return buf;
}

std::string join_comma(const std::vector<std::string> &parts)
{
    std::string out;
    for (const std::string &p : parts)
    {
        if (p.empty())
        {
            continue;
        }
        if (!out.empty())
        {
            out += ",";
        }
        out += p;
    }
    return out;
}

void add_unique_mark(std::vector<double> *marks, double t, double t0, double t1)
{
    if (t < t0)
    {
        t = t0;
    }
    if (t > t1)
    {
        t = t1;
    }
    marks->push_back(t);
}

void add_range_marks(std::vector<double> *marks, const std::vector<vw_range> &ranges, double t0,
                     double t1)
{
    for (const vw_range &r : ranges)
    {
        if (r.end_s <= t0 || r.start_s >= t1)
        {
            continue;
        }
        add_unique_mark(marks, r.start_s, t0, t1);
        add_unique_mark(marks, r.end_s, t0, t1);
    }
}

std::vector<double> unique_sorted(std::vector<double> marks)
{
    std::sort(marks.begin(), marks.end());
    static const double k_eps = 1e-7;
    std::vector<double> out;
    for (double t : marks)
    {
        if (out.empty() || std::fabs(t - out.back()) > k_eps)
        {
            out.push_back(t);
        }
    }
    return out;
}

bool cover_mid(const std::vector<vw_range> &ranges, bool enabled, double mid)
{
    if (!enabled)
    {
        return false;
    }
    return vw_ranges_cover(ranges.data(), ranges.size(), mid) != 0;
}

std::string crop_filter(const vw_crop &crop)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "crop=%d:%d:%d:%d", crop.width, crop.height, crop.x, crop.y);
    return buf;
}

struct segment
{
    double start = 0.0;
    double end = 0.0;
    bool gray = false;
    bool reverse = false;
    bool crop = false;
    bool mute = false;
    double speed = 1.0;
};

std::vector<segment> build_segments(const cli_options &opt, const time_window &window,
                                    double duration_s)
{
    double t0 = 0.0;
    double t1 = duration_s;
    if (window.enabled)
    {
        t0 = window.start_s;
        t1 = window.end_s;
    }
    if (t1 <= t0)
    {
        t1 = t0 + 1e-3;
    }

    std::vector<double> marks;
    marks.push_back(t0);
    marks.push_back(t1);
    add_range_marks(&marks, opt.reverse_ranges, t0, t1);
    add_range_marks(&marks, opt.speed_ranges, t0, t1);
    add_range_marks(&marks, opt.crop_ranges, t0, t1);
    add_range_marks(&marks, opt.grayscale_ranges, t0, t1);
    add_range_marks(&marks, opt.mute_ranges, t0, t1);
    marks = unique_sorted(std::move(marks));

    const double speed = opt.speed_factor.value_or(1.0);
    std::vector<segment> segs;
    for (std::size_t i = 0; i + 1 < marks.size(); i++)
    {
        segment s{};
        s.start = marks[i];
        s.end = marks[i + 1];
        if (s.end - s.start < 1e-6)
        {
            continue;
        }
        const double mid = (s.start + s.end) * 0.5;
        s.gray = cover_mid(opt.grayscale_ranges, opt.grayscale, mid);
        s.reverse = cover_mid(opt.reverse_ranges, opt.reverse, mid);
        s.crop = cover_mid(opt.crop_ranges, opt.crop.has_value(), mid);
        s.mute = cover_mid(opt.mute_ranges, opt.mute, mid);
        if (opt.speed_factor.has_value() && cover_mid(opt.speed_ranges, true, mid))
        {
            s.speed = speed;
        }
        segs.push_back(s);
    }
    return segs;
}

std::string fps_filter(int fps_num, int fps_den)
{
    if (fps_num <= 0 || fps_den <= 0)
    {
        return {};
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "fps=%d/%d", fps_num, fps_den);
    return buf;
}

std::string video_effects(const segment &s, const vw_crop *crop, int src_w, int src_h,
                          bool whole_crop, int fps_num, int fps_den, bool include_gray)
{
    std::vector<std::string> parts;
    if (whole_crop && crop != nullptr)
    {
        parts.push_back(crop_filter(*crop));
    }
    else if (s.crop && crop != nullptr)
    {
        /* Real crop: keep the rectangle, pad the rest black (not a zoom). */
        parts.push_back(crop_filter(*crop));
        char pad[96];
        std::snprintf(pad, sizeof(pad), "pad=%d:%d:%d:%d:black", src_w, src_h, crop->x, crop->y);
        parts.push_back(pad);
    }
    if (include_gray && s.gray)
    {
        parts.push_back(grayscale_filter({}));
    }
    if (s.reverse)
    {
        parts.push_back("reverse");
    }
    if (s.speed != 1.0)
    {
        parts.push_back(std::string("setpts=PTS/") + fmt_f(s.speed));
        const std::string fps = fps_filter(fps_num, fps_den);
        if (!fps.empty())
        {
            parts.push_back(fps);
        }
    }
    return join_comma(parts);
}

std::string finish_audio(const std::string &chain)
{
    /* atempo emits packed FLT; native AAC requires planar FLTP. */
    if (chain.empty())
    {
        return chain;
    }
    return chain + ",aformat=sample_fmts=fltp";
}

std::string audio_effects(const segment &s)
{
    std::vector<std::string> parts;
    if (s.mute)
    {
        parts.push_back("volume=0");
    }
    if (s.reverse)
    {
        parts.push_back("areverse");
    }
    if (s.speed != 1.0)
    {
        parts.push_back(atempo_chain(s.speed));
    }
    return join_comma(parts);
}

} // namespace

std::string atempo_chain(double factor)
{
    std::string out;
    double f = factor;
    if (!(f > 0.0) || !std::isfinite(f))
    {
        return "anull";
    }

    auto append = [&](double x) {
        if (!out.empty())
        {
            out += ",";
        }
        out += "atempo=";
        out += fmt_f(x);
    };

    while (f > 2.0000001)
    {
        append(2.0);
        f /= 2.0;
    }
    while (f < 0.5 - 1e-9)
    {
        append(0.5);
        f /= 0.5;
    }
    append(f);
    return out;
}

std::string enable_expression(const std::vector<vw_range> &ranges)
{
    if (ranges.empty())
    {
        return {};
    }
    std::string e;
    for (std::size_t i = 0; i < ranges.size(); i++)
    {
        if (i > 0)
        {
            e += "+";
        }
        if (ranges[i].end_s < 0.0)
        {
            e += "gte(t,";
            e += fmt_t(ranges[i].start_s);
            e += ")";
        }
        else
        {
            e += "between(t,";
            e += fmt_t(ranges[i].start_s);
            e += ",";
            e += fmt_t(ranges[i].end_s);
            e += ")";
        }
    }
    return e;
}

std::string grayscale_filter(const std::vector<vw_range> &ranges)
{
    /* Rec.709 luma, applied in gbrp then mixed onto R=G=B. */
    std::string f =
        "format=gbrp,colorchannelmixer=rr=0.2126:rg=0.7152:rb=0.0722:"
        "gr=0.2126:gg=0.7152:gb=0.0722:br=0.2126:bg=0.7152:bb=0.0722";
    const std::string en = enable_expression(ranges);
    if (!en.empty())
    {
        f += ":enable='";
        f += en;
        f += "'";
    }
    return f;
}

std::string drawtext_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        if (c == '\\' || c == ':' || c == '\'' || c == '%')
        {
            out += '\\';
        }
        out += c;
    }
    return out;
}

std::string default_font_file(const cli_options &opt)
{
    namespace fs = std::filesystem;
    if (!opt.text_font.empty())
    {
        return opt.text_font;
    }
    const bool bold = opt.text_style != "regular";
    const char *bold_path = "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf";
    const char *reg_path = "/usr/share/fonts/dejavu/DejaVuSans.ttf";
    const char *pick = bold ? bold_path : reg_path;
    if (fs::is_regular_file(pick))
    {
        return pick;
    }
    if (fs::is_regular_file(bold_path))
    {
        return bold_path;
    }
    if (fs::is_regular_file(reg_path))
    {
        return reg_path;
    }
    return pick;
}

std::string text_overlays_filter(const cli_options &opt)
{
    if (opt.texts.empty())
    {
        return {};
    }
    const std::string font = default_font_file(opt);
    std::string chain;
    for (const vw_text &tx : opt.texts)
    {
        std::string d = "drawtext=fontfile=" + drawtext_escape(font);
        d += ":text='" + drawtext_escape(tx.text) + "'";
        d += ":x=" + std::to_string(tx.x);
        d += ":y=" + std::to_string(tx.y);
        d += ":fontsize=" + std::to_string(opt.text_size < 8 ? 28 : opt.text_size);
        d += ":fontcolor=" + (opt.text_color.empty() ? std::string("#ffffff") : opt.text_color);
        if (!opt.text_bg.empty())
        {
            d += ":box=1:boxcolor=" + opt.text_bg + ":boxborderw=8";
        }
        if (tx.has_range != 0)
        {
            const double t1 = tx.range.end_s < 0.0 ? 1000000000000.0 : tx.range.end_s;
            d += ":enable='gte(t," + fmt_t(tx.range.start_s) + ")*lt(t," + fmt_t(t1) + ")'";
        }
        if (!chain.empty())
        {
            chain += ",";
        }
        chain += d;
    }
    return chain;
}

std::string zoom_time_expr(int fps_num, int fps_den)
{
    /* zoompan has no `t`; use input frame index. */
    if (fps_num <= 0 || fps_den <= 0)
    {
        return "(in/30)";
    }
    return "(in*" + fmt_f(static_cast<double>(fps_den) / static_cast<double>(fps_num)) + ")";
}

std::string zoom_gate(const vw_range &r, const std::string &texpr)
{
    const double t1 = r.end_s < 0.0 ? 1000000000000.0 : r.end_s;
    return "(gte(" + texpr + "," + fmt_t(r.start_s) + ")*lt(" + texpr + "," + fmt_t(t1) + "))";
}

std::string zoom_filter(const std::vector<vw_zoom_seg> &segs, int w, int h, int fps_num,
                        int fps_den)
{
    if (segs.empty() || w < 2 || h < 2)
    {
        return {};
    }

    /* Comma-free expressions so avfilter_graph_parse_ptr does not split them. */
    const std::string texpr = zoom_time_expr(fps_num, fps_den);
    std::string z = "1";
    std::string cx = "0.5";
    std::string cy = "0.5";
    for (const vw_zoom_seg &s : segs)
    {
        const double t0 = s.range.start_s;
        const double t1raw = s.range.end_s < 0.0 ? t0 + 1.0 : s.range.end_s;
        const double dur = t1raw - t0 < 1e-6 ? 1e-6 : t1raw - t0;
        const std::string gate = zoom_gate(s.range, texpr);
        std::string zi;
        if (std::fabs(s.z1 - s.z0) < 1e-9)
        {
            zi = fmt_f(s.z0);
        }
        else
        {
            zi = "(" + fmt_f(s.z0) + "+(" + fmt_f(s.z1 - s.z0) + ")*(" + texpr + "-" + fmt_t(t0) +
                 ")/" + fmt_f(dur) + ")";
        }
        z += "+" + gate + "*(" + zi + "-1)";
        cx += "+" + gate + "*(" + fmt_f(s.cx) + "-0.5)";
        cy += "+" + gate + "*(" + fmt_f(s.cy) + "-0.5)";
    }

    const std::string rawx = "(iw*(" + cx + ")-iw/zoom/2)";
    const std::string limx = "(iw-iw/zoom)";
    const std::string x =
        "(" + rawx + "*gte(" + rawx + ",0)*lte(" + rawx + "," + limx + ")+" + limx + "*gt(" + rawx +
        "," + limx + "))";
    const std::string rawy = "(ih*(" + cy + ")-ih/zoom/2)";
    const std::string limy = "(ih-ih/zoom)";
    const std::string y =
        "(" + rawy + "*gte(" + rawy + ",0)*lte(" + rawy + "," + limy + ")+" + limy + "*gt(" + rawy +
        "," + limy + "))";

    char swh[64];
    std::snprintf(swh, sizeof(swh), "%dx%d", w & ~1, h & ~1);
    std::string fps = "30";
    if (fps_num > 0 && fps_den > 0)
    {
        char b[64];
        std::snprintf(b, sizeof(b), "%d/%d", fps_num, fps_den);
        fps = b;
    }

    return std::string("zoompan=z='") + z + "':x='" + x + "':y='" + y + "':d=1:s=" + swh +
           ":fps=" + fps;
}

bool needs_segment_graph(const cli_options &opt)
{
    if (opt.reverse && !opt.reverse_ranges.empty())
    {
        return true;
    }
    if (opt.speed_factor.has_value() && !opt.speed_ranges.empty())
    {
        return true;
    }
    if (opt.crop.has_value() && !opt.crop_ranges.empty())
    {
        return true;
    }
    return false;
}

filter_graphs build_filter_graphs(const cli_options &opt, const time_window &window, int src_w,
                                  int src_h, double duration_s, const vw_crop *crop_resolved,
                                  int fps_num, int fps_den)
{
    filter_graphs g{};
    const bool mute_all = opt.mute && opt.mute_ranges.empty();
    g.drop_audio = mute_all;

    if (needs_segment_graph(opt))
    {
        const std::vector<segment> segs = build_segments(opt, window, duration_s);
        if (segs.empty())
        {
            g.video = "null";
            g.audio = mute_all ? "" : finish_audio("anull");
            g.drop_audio = mute_all;
            return g;
        }

        if (segs.size() == 1)
        {
            std::string ve = video_effects(segs[0], crop_resolved, src_w, src_h, false, fps_num,
                                           fps_den, false);
            std::vector<std::string> pre;
            const std::string zf = zoom_filter(opt.zoom, src_w, src_h, fps_num, fps_den);
            if (!zf.empty())
            {
                pre.push_back(zf);
            }
            if (opt.grayscale)
            {
                pre.push_back(grayscale_filter(opt.grayscale_ranges));
            }
            const std::string tf = text_overlays_filter(opt);
            if (!tf.empty())
            {
                pre.push_back(tf);
            }
            const std::string head = join_comma(pre);
            if (!head.empty())
            {
                ve = ve.empty() || ve == "null" ? head : head + "," + ve;
            }
            g.video = ve.empty() ? "null" : ve;
            if (mute_all)
            {
                g.drop_audio = true;
                g.audio.clear();
            }
            else
            {
                std::string ae = audio_effects(segs[0]);
                g.audio = finish_audio(ae.empty() ? "anull" : ae);
            }
            return g;
        }

        g.uses_split = true;
        const int n = static_cast<int>(segs.size());
        std::ostringstream v;
        {
            const std::string zf = zoom_filter(opt.zoom, src_w, src_h, fps_num, fps_den);
            if (!zf.empty())
            {
                v << zf << ",";
            }
            if (opt.grayscale)
            {
                v << grayscale_filter(opt.grayscale_ranges) << ",";
            }
            const std::string tf = text_overlays_filter(opt);
            if (!tf.empty())
            {
                v << tf << ",";
            }
        }
        v << "split=" << n;
        for (int i = 0; i < n; i++)
        {
            v << "[v" << i << "]";
        }
        v << ";";
        for (int i = 0; i < n; i++)
        {
            v << "[v" << i << "]trim=" << fmt_t(segs[static_cast<std::size_t>(i)].start) << ":"
              << fmt_t(segs[static_cast<std::size_t>(i)].end) << ",setpts=PTS-STARTPTS";
            const std::string fx =
                video_effects(segs[static_cast<std::size_t>(i)], crop_resolved, src_w, src_h, false,
                              fps_num, fps_den, false);
            if (!fx.empty())
            {
                v << "," << fx;
            }
            v << "[s" << i << "];";
        }
        for (int i = 0; i < n; i++)
        {
            v << "[s" << i << "]";
        }
        v << "concat=n=" << n << ":v=1:a=0";
        g.video = v.str();

        if (mute_all)
        {
            g.drop_audio = true;
            g.audio.clear();
        }
        else
        {
            std::ostringstream a;
            a << "asplit=" << n;
            for (int i = 0; i < n; i++)
            {
                a << "[a" << i << "]";
            }
            a << ";";
            for (int i = 0; i < n; i++)
            {
                a << "[a" << i << "]atrim=" << fmt_t(segs[static_cast<std::size_t>(i)].start) << ":"
                  << fmt_t(segs[static_cast<std::size_t>(i)].end) << ",asetpts=PTS-STARTPTS";
                const std::string fx = audio_effects(segs[static_cast<std::size_t>(i)]);
                if (!fx.empty())
                {
                    a << "," << fx;
                }
                a << "[t" << i << "];";
            }
            for (int i = 0; i < n; i++)
            {
                a << "[t" << i << "]";
            }
            a << "concat=n=" << n << ":v=0:a=1,aformat=sample_fmts=fltp";
            g.audio = a.str();
        }
        return g;
    }

    /* Simple chain: original timestamps, enable= for ranged gray/mute. */
    std::vector<std::string> vparts;
    {
        const std::string zf = zoom_filter(opt.zoom, src_w, src_h, fps_num, fps_den);
        if (!zf.empty())
        {
            vparts.push_back(zf);
        }
    }
    const bool whole_crop = opt.crop.has_value() && opt.crop_ranges.empty();
    if (whole_crop && crop_resolved != nullptr)
    {
        vparts.push_back(crop_filter(*crop_resolved));
    }
    if (opt.grayscale)
    {
        vparts.push_back(grayscale_filter(opt.grayscale_ranges));
    }
    {
        const std::string tf = text_overlays_filter(opt);
        if (!tf.empty())
        {
            vparts.push_back(tf);
        }
    }
    if (opt.reverse)
    {
        vparts.push_back("reverse");
    }
    if (opt.speed_factor.has_value() && *opt.speed_factor != 1.0)
    {
        vparts.push_back(std::string("setpts=PTS/") + fmt_f(*opt.speed_factor));
        const std::string fps = fps_filter(fps_num, fps_den);
        if (!fps.empty())
        {
            vparts.push_back(fps);
        }
    }
    g.video = vparts.empty() ? "null" : join_comma(vparts);

    if (mute_all)
    {
        g.drop_audio = true;
        g.audio.clear();
        return g;
    }

    std::vector<std::string> aparts;
    if (opt.mute && !opt.mute_ranges.empty())
    {
        aparts.push_back(std::string("volume=0:enable='") + enable_expression(opt.mute_ranges) +
                         "'");
    }
    if (opt.reverse)
    {
        aparts.push_back("areverse");
    }
    if (opt.speed_factor.has_value() && *opt.speed_factor != 1.0)
    {
        aparts.push_back(atempo_chain(*opt.speed_factor));
    }
    g.audio = finish_audio(aparts.empty() ? "anull" : join_comma(aparts));
    return g;
}

} // namespace vidwizard
