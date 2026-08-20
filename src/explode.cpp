/**
 * @file explode.cpp
 * @brief High-quality 32-bit max-compressed PNG frame export (threaded).
 */

#include "vidwizard/media.hpp"

#include "vidwizard/log.hpp"
#include "vidwizard/parse_time.h"
#include "vidwizard/paths.hpp"
#include "vidwizard/threads.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace vidwizard
{
namespace
{

std::string av_err(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

double frame_time_s(const AVStream *st, int64_t ts)
{
    if (st == nullptr || ts == AV_NOPTS_VALUE)
    {
        return 0.0;
    }
    return static_cast<double>(ts) * av_q2d(st->time_base);
}

int write_rgba_png(const char *path, const uint8_t *rgba, int width, int height, int stride)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    AVCodecContext *ctx = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    FILE *fp = nullptr;
    int ret = 0;
    int y = 0;

    if (codec == nullptr)
    {
        log_error("vidwizard: PNG encoder not found");
        return -1;
    }
    ctx = avcodec_alloc_context3(codec);
    if (ctx == nullptr)
    {
        return AVERROR(ENOMEM);
    }
    ctx->width = width;
    ctx->height = height;
    ctx->pix_fmt = AV_PIX_FMT_RGBA;
    ctx->time_base = av_make_q(1, 25);
    ctx->compression_level = 9;
    if (ctx->priv_data != nullptr)
    {
        av_opt_set(ctx->priv_data, "pred", "mixed", 0);
    }
    ret = avcodec_open2(ctx, codec, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: open png: %s", av_err(ret).c_str());
        avcodec_free_context(&ctx);
        return ret;
    }

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (frame == nullptr || pkt == nullptr)
    {
        ret = AVERROR(ENOMEM);
        goto done;
    }
    frame->format = AV_PIX_FMT_RGBA;
    frame->width = width;
    frame->height = height;
    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0)
    {
        goto done;
    }
    ret = av_frame_make_writable(frame);
    if (ret < 0)
    {
        goto done;
    }
    for (y = 0; y < height; y++)
    {
        std::memcpy(frame->data[0] + static_cast<std::size_t>(y) * static_cast<std::size_t>(frame->linesize[0]),
                    rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride),
                    static_cast<std::size_t>(width) * 4U);
    }
    frame->pts = 0;
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0)
    {
        goto done;
    }
    ret = avcodec_send_frame(ctx, nullptr);
    if (ret < 0)
    {
        goto done;
    }
    ret = avcodec_receive_packet(ctx, pkt);
    if (ret < 0)
    {
        log_error("vidwizard: png encode: %s", av_err(ret).c_str());
        goto done;
    }

    fp = std::fopen(path, "wb");
    if (fp == nullptr)
    {
        log_error("vidwizard: cannot write %s", path);
        ret = AVERROR(EIO);
        goto done;
    }
    if (std::fwrite(pkt->data, 1, static_cast<std::size_t>(pkt->size), fp) !=
        static_cast<std::size_t>(pkt->size))
    {
        log_error("vidwizard: short write %s", path);
        ret = AVERROR(EIO);
        goto done;
    }
    ret = 0;

done:
    if (fp != nullptr)
    {
        std::fclose(fp);
        fp = nullptr;
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return ret;
}

int64_t count_frames(const std::filesystem::path &input, const std::vector<vw_range> &ranges,
                     unsigned jobs)
{
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    const AVCodec *codec = nullptr;
    int vidx = -1;
    int ret = 0;
    int64_t count = 0;

    ret = avformat_open_input(&fmt, input.c_str(), nullptr, nullptr);
    if (ret < 0)
    {
        return -1;
    }
    ret = avformat_find_stream_info(fmt, nullptr);
    if (ret < 0)
    {
        avformat_close_input(&fmt);
        return -1;
    }
    vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (vidx < 0 || codec == nullptr)
    {
        avformat_close_input(&fmt);
        return -1;
    }
    dec = avcodec_alloc_context3(codec);
    if (dec == nullptr)
    {
        avformat_close_input(&fmt);
        return -1;
    }
    avcodec_parameters_to_context(dec, fmt->streams[vidx]->codecpar);
    dec->pkt_timebase = fmt->streams[vidx]->time_base;
    dec->thread_count = static_cast<int>(jobs);
    if (avcodec_open2(dec, codec, nullptr) < 0)
    {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return -1;
    }
    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (frame == nullptr || pkt == nullptr)
    {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return -1;
    }

    auto consider = [&](AVFrame *fr) {
        const double t = frame_time_s(fmt->streams[vidx], fr->best_effort_timestamp);
        if (vw_ranges_cover(ranges.data(), ranges.size(), t) != 0)
        {
            count++;
        }
    };

    while (av_read_frame(fmt, pkt) >= 0)
    {
        if (pkt->stream_index != vidx)
        {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(dec, pkt) < 0)
        {
            av_packet_unref(pkt);
            break;
        }
        av_packet_unref(pkt);
        while (avcodec_receive_frame(dec, frame) >= 0)
        {
            consider(frame);
            av_frame_unref(frame);
        }
    }
    avcodec_send_packet(dec, nullptr);
    while (avcodec_receive_frame(dec, frame) >= 0)
    {
        consider(frame);
        av_frame_unref(frame);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return count;
}

int init_rgba_filter(AVFilterGraph **graph, AVFilterContext **src, AVFilterContext **sink,
                     AVCodecContext *dec, const char *spec, unsigned jobs)
{
    char args[512];
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    const AVFilter *src_f = avfilter_get_by_name("buffer");
    const AVFilter *sink_f = avfilter_get_by_name("buffersink");
    enum AVPixelFormat pix = AV_PIX_FMT_RGBA;
    int ret = 0;

    *graph = avfilter_graph_alloc();
    if (*graph == nullptr || outputs == nullptr || inputs == nullptr)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    (*graph)->nb_threads = static_cast<int>(jobs);

    std::snprintf(args, sizeof(args),
                  "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d", dec->width,
                  dec->height, dec->pix_fmt, dec->pkt_timebase.num, dec->pkt_timebase.den,
                  dec->sample_aspect_ratio.num, dec->sample_aspect_ratio.den);
    ret = avfilter_graph_create_filter(src, src_f, "in", args, nullptr, *graph);
    if (ret < 0)
    {
        goto fail;
    }
    *sink = avfilter_graph_alloc_filter(*graph, sink_f, "out");
    if (*sink == nullptr)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ret = av_opt_set_bin(*sink, "pix_fmts", reinterpret_cast<uint8_t *>(&pix), sizeof(pix),
                         AV_OPT_SEARCH_CHILDREN);
    if (ret < 0)
    {
        goto fail;
    }
    ret = avfilter_init_dict(*sink, nullptr);
    if (ret < 0)
    {
        goto fail;
    }
    outputs->name = av_strdup("in");
    outputs->filter_ctx = *src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = *sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    ret = avfilter_graph_parse_ptr(*graph, spec, &inputs, &outputs, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: explode filter parse (%s): %s", spec, av_err(ret).c_str());
        goto fail;
    }
    ret = avfilter_graph_config(*graph, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: explode filter config: %s", av_err(ret).c_str());
        goto fail;
    }
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return 0;

fail:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    avfilter_graph_free(graph);
    *src = nullptr;
    *sink = nullptr;
    return ret;
}

} // namespace

int explode_file(const std::filesystem::path &input, const std::filesystem::path &prefix,
                 const cli_options &opt, unsigned jobs)
{
    const std::vector<vw_range> &ranges = opt.explode_ranges;
    const int64_t nframes = count_frames(input, ranges, jobs);
    if (nframes < 0)
    {
        log_error("vidwizard: cannot count frames in %s", input.c_str());
        return 1;
    }
    if (nframes == 0)
    {
        log_error("vidwizard: no frames to explode in %s", input.c_str());
        return 1;
    }

    const int width_digits = vw_frame_index_width(static_cast<uint64_t>(nframes));
    log_verbose("vidwizard: exploding %lld frames (index width %d) from %s",
                static_cast<long long>(nframes), width_digits, input.c_str());

    if (ensure_parent_directory(std::filesystem::path(prefix.string() + "_1.png")) != 0)
    {
        return 1;
    }

    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *filt = nullptr;
    AVPacket *pkt = nullptr;
    AVFilterGraph *graph = nullptr;
    AVFilterContext *src = nullptr;
    AVFilterContext *sink = nullptr;
    const AVCodec *codec = nullptr;
    int vidx = -1;
    int ret = 0;
    vw_crop crop{};
    const vw_crop *crop_ptr = nullptr;
    uint64_t index = 1;
    std::atomic<int> fail_count{0};
    std::mutex cap_mu;
    std::condition_variable cap_cv;
    unsigned inflight = 0;
    const unsigned cap = std::max(2U, jobs * 3U);
    thread_pool pool(jobs);

    ret = avformat_open_input(&fmt, input.c_str(), nullptr, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: cannot open %s: %s", input.c_str(), av_err(ret).c_str());
        return 1;
    }
    ret = avformat_find_stream_info(fmt, nullptr);
    if (ret < 0)
    {
        avformat_close_input(&fmt);
        return 1;
    }
    vidx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (vidx < 0 || codec == nullptr)
    {
        avformat_close_input(&fmt);
        return 1;
    }
    dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[vidx]->codecpar);
    dec->pkt_timebase = fmt->streams[vidx]->time_base;
    dec->thread_count = static_cast<int>(jobs);
    dec->framerate = av_guess_frame_rate(fmt, fmt->streams[vidx], nullptr);
    ret = avcodec_open2(dec, codec, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: decoder: %s", av_err(ret).c_str());
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return 1;
    }

    if (opt.crop.has_value())
    {
        crop = *opt.crop;
        if (resolve_crop(&crop, dec->width, dec->height) != 0)
        {
            avcodec_free_context(&dec);
            avformat_close_input(&fmt);
            return 1;
        }
        crop_ptr = &crop;
    }

    {
        /* Explode ignores mute/speed (timing-only / audio). When a frame
         * subset is already selected, other ops apply to that subset as a whole. */
        cli_options fopt = opt;
        fopt.mute = false;
        fopt.speed_factor.reset();
        fopt.cut = false;
        if (!opt.explode_ranges.empty())
        {
            fopt.grayscale_ranges.clear();
            fopt.reverse_ranges.clear();
            fopt.crop_ranges.clear();
        }
        const double dur = (fmt->duration > 0)
                               ? static_cast<double>(fmt->duration) / static_cast<double>(AV_TIME_BASE)
                               : 0.0;
        const filter_graphs g =
            build_filter_graphs(fopt, time_window{}, dec->width, dec->height, dur, crop_ptr,
                                dec->framerate.num, dec->framerate.den);
        ret = init_rgba_filter(&graph, &src, &sink, dec, g.video.c_str(), jobs);
        if (ret < 0)
        {
            avcodec_free_context(&dec);
            avformat_close_input(&fmt);
            return 1;
        }
    }

    frame = av_frame_alloc();
    filt = av_frame_alloc();
    pkt = av_packet_alloc();
    if (frame == nullptr || filt == nullptr || pkt == nullptr)
    {
        av_frame_free(&frame);
        av_frame_free(&filt);
        av_packet_free(&pkt);
        avfilter_graph_free(&graph);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return 1;
    }

    auto submit_png = [&](AVFrame *rgba) {
        const int w = rgba->width;
        const int h = rgba->height;
        const int stride = w * 4;
        std::vector<uint8_t> buf(static_cast<std::size_t>(stride) * static_cast<std::size_t>(h));
        for (int y = 0; y < h; y++)
        {
            std::memcpy(buf.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride),
                        rgba->data[0] + static_cast<std::size_t>(y) *
                                            static_cast<std::size_t>(rgba->linesize[0]),
                        static_cast<std::size_t>(stride));
        }
        char name[4096];
        if (vw_format_frame_name(name, sizeof(name), prefix.c_str(), width_digits, index) != 0)
        {
            fail_count.fetch_add(1);
            return;
        }
        index++;
        {
            std::unique_lock<std::mutex> lock(cap_mu);
            cap_cv.wait(lock, [&]() { return inflight < cap || fail_count.load() > 0; });
            inflight++;
        }
        std::string path = name;
        pool.submit([path = std::move(path), buf = std::move(buf), w, h, stride, &fail_count,
                     &cap_mu, &cap_cv, &inflight]() mutable {
            if (write_rgba_png(path.c_str(), buf.data(), w, h, stride) != 0)
            {
                fail_count.fetch_add(1);
            }
            {
                std::lock_guard<std::mutex> lock(cap_mu);
                inflight--;
            }
            cap_cv.notify_all();
        });
    };

    auto feed = [&](AVFrame *fr) {
        const double t = frame_time_s(fmt->streams[vidx], fr->best_effort_timestamp);
        if (vw_ranges_cover(ranges.data(), ranges.size(), t) == 0)
        {
            return 0;
        }
        fr->pts = fr->best_effort_timestamp;
        ret = av_buffersrc_add_frame_flags(src, fr, 0);
        if (ret < 0)
        {
            return ret;
        }
        while (true)
        {
            ret = av_buffersink_get_frame(sink, filt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                return 0;
            }
            if (ret < 0)
            {
                return ret;
            }
            submit_png(filt);
            av_frame_unref(filt);
        }
    };

    while (av_read_frame(fmt, pkt) >= 0)
    {
        if (pkt->stream_index != vidx)
        {
            av_packet_unref(pkt);
            continue;
        }
        ret = avcodec_send_packet(dec, pkt);
        av_packet_unref(pkt);
        if (ret < 0)
        {
            goto done;
        }
        while (avcodec_receive_frame(dec, frame) >= 0)
        {
            ret = feed(frame);
            av_frame_unref(frame);
            if (ret < 0)
            {
                goto done;
            }
        }
    }
    avcodec_send_packet(dec, nullptr);
    while (avcodec_receive_frame(dec, frame) >= 0)
    {
        ret = feed(frame);
        av_frame_unref(frame);
        if (ret < 0)
        {
            goto done;
        }
    }
    ret = av_buffersrc_add_frame_flags(src, nullptr, 0);
    if (ret < 0)
    {
        log_error("vidwizard: explode flush: %s", av_err(ret).c_str());
        goto done;
    }
    while (av_buffersink_get_frame(sink, filt) >= 0)
    {
        submit_png(filt);
        av_frame_unref(filt);
    }

    pool.wait_idle();
    if (fail_count.load() > 0)
    {
        log_error("vidwizard: PNG encode failed for %d frame(s)", fail_count.load());
        ret = 1;
        goto done;
    }
    log_error("vidwizard: wrote %llu PNG frame(s) with prefix %s",
              static_cast<unsigned long long>(index - 1ULL), prefix.c_str());
    ret = 0;

done:
    av_frame_free(&frame);
    av_frame_free(&filt);
    av_packet_free(&pkt);
    avfilter_graph_free(&graph);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return ret == 0 ? 0 : 1;
}

} // namespace vidwizard
