/**
 * @file transcode.cpp
 * @brief Decode → libavfilter → encode/mux using FFmpeg 8 libraries.
 */

#include "vidwizard/media.hpp"

#include "vidwizard/log.hpp"
#include "vidwizard/paths.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/rational.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cstring>
#include <string>

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

double packet_time_s(const AVStream *st, int64_t ts)
{
    if (st == nullptr || ts == AV_NOPTS_VALUE)
    {
        return 0.0;
    }
    return static_cast<double>(ts) * av_q2d(st->time_base);
}

double duration_seconds(AVFormatContext *fmt, const AVStream *st)
{
    if (fmt != nullptr && fmt->duration > 0)
    {
        return static_cast<double>(fmt->duration) / static_cast<double>(AV_TIME_BASE);
    }
    if (st != nullptr && st->duration > 0)
    {
        return static_cast<double>(st->duration) * av_q2d(st->time_base);
    }
    return 0.0;
}

struct stream_pipe
{
    int in_index = -1;
    int out_index = -1;
    AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    AVCodecContext *dec = nullptr;
    AVCodecContext *enc = nullptr;
    AVFrame *dec_frame = nullptr;
    AVFilterGraph *graph = nullptr;
    AVFilterContext *src = nullptr;
    AVFilterContext *sink = nullptr;
    AVFrame *filt_frame = nullptr;
    AVPacket *enc_pkt = nullptr;
    bool past_window = false;
    int64_t next_pts = 0;
};

void free_pipe(stream_pipe *p)
{
    if (p == nullptr)
    {
        return;
    }
    avfilter_graph_free(&p->graph);
    avcodec_free_context(&p->dec);
    avcodec_free_context(&p->enc);
    av_frame_free(&p->dec_frame);
    av_frame_free(&p->filt_frame);
    av_packet_free(&p->enc_pkt);
}

int open_decoder(AVFormatContext *fmt, int index, unsigned jobs, stream_pipe *out)
{
    AVStream *st = fmt->streams[index];
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (dec == nullptr)
    {
        log_error("vidwizard: no decoder for stream %d", index);
        return AVERROR_DECODER_NOT_FOUND;
    }
    out->dec = avcodec_alloc_context3(dec);
    if (out->dec == nullptr)
    {
        return AVERROR(ENOMEM);
    }
    int ret = avcodec_parameters_to_context(out->dec, st->codecpar);
    if (ret < 0)
    {
        return ret;
    }
    out->dec->pkt_timebase = st->time_base;
    out->dec->thread_count = static_cast<int>(jobs);
    out->dec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (out->dec->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        out->dec->framerate = av_guess_frame_rate(fmt, st, nullptr);
    }
    ret = avcodec_open2(out->dec, dec, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: avcodec_open2 decoder: %s", av_err(ret).c_str());
        return ret;
    }
    out->dec_frame = av_frame_alloc();
    out->filt_frame = av_frame_alloc();
    out->enc_pkt = av_packet_alloc();
    if (out->dec_frame == nullptr || out->filt_frame == nullptr || out->enc_pkt == nullptr)
    {
        return AVERROR(ENOMEM);
    }
    out->in_index = index;
    out->type = out->dec->codec_type;
    return 0;
}

int init_filter(stream_pipe *p, const char *filter_spec, unsigned jobs)
{
    char args[512];
    int ret = 0;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    const AVFilter *src_f = nullptr;
    const AVFilter *sink_f = nullptr;

    p->graph = avfilter_graph_alloc();
    if (p->graph == nullptr || outputs == nullptr || inputs == nullptr)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    p->graph->nb_threads = static_cast<int>(jobs);

    if (p->type == AVMEDIA_TYPE_VIDEO)
    {
        src_f = avfilter_get_by_name("buffer");
        sink_f = avfilter_get_by_name("buffersink");
        std::snprintf(args, sizeof(args),
                      "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                      p->dec->width, p->dec->height, p->dec->pix_fmt, p->dec->pkt_timebase.num,
                      p->dec->pkt_timebase.den, p->dec->sample_aspect_ratio.num,
                      p->dec->sample_aspect_ratio.den);
        ret = avfilter_graph_create_filter(&p->src, src_f, "in", args, nullptr, p->graph);
        if (ret < 0)
        {
            goto fail;
        }
        p->sink = avfilter_graph_alloc_filter(p->graph, sink_f, "out");
        if (p->sink == nullptr)
        {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ret = av_opt_set_bin(p->sink, "pix_fmts", reinterpret_cast<uint8_t *>(&p->enc->pix_fmt),
                             sizeof(p->enc->pix_fmt), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0)
        {
            goto fail;
        }
        ret = avfilter_init_dict(p->sink, nullptr);
        if (ret < 0)
        {
            goto fail;
        }
    }
    else
    {
        char layout[64];
        src_f = avfilter_get_by_name("abuffer");
        sink_f = avfilter_get_by_name("abuffersink");
        if (p->dec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        {
            av_channel_layout_default(&p->dec->ch_layout, p->dec->ch_layout.nb_channels);
        }
        av_channel_layout_describe(&p->dec->ch_layout, layout, sizeof(layout));
        std::snprintf(args, sizeof(args),
                      "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                      p->dec->pkt_timebase.num, p->dec->pkt_timebase.den, p->dec->sample_rate,
                      av_get_sample_fmt_name(p->dec->sample_fmt), layout);
        ret = avfilter_graph_create_filter(&p->src, src_f, "in", args, nullptr, p->graph);
        if (ret < 0)
        {
            goto fail;
        }
        p->sink = avfilter_graph_alloc_filter(p->graph, sink_f, "out");
        if (p->sink == nullptr)
        {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ret = av_opt_set_bin(p->sink, "sample_fmts",
                             reinterpret_cast<uint8_t *>(&p->enc->sample_fmt),
                             sizeof(p->enc->sample_fmt), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0)
        {
            goto fail;
        }
        av_channel_layout_describe(&p->enc->ch_layout, layout, sizeof(layout));
        ret = av_opt_set(p->sink, "ch_layouts", layout, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0)
        {
            goto fail;
        }
        ret = av_opt_set_bin(p->sink, "sample_rates",
                             reinterpret_cast<uint8_t *>(&p->enc->sample_rate),
                             sizeof(p->enc->sample_rate), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0)
        {
            goto fail;
        }
        if (p->enc->frame_size > 0)
        {
            av_buffersink_set_frame_size(p->sink, static_cast<unsigned>(p->enc->frame_size));
        }
        ret = avfilter_init_dict(p->sink, nullptr);
        if (ret < 0)
        {
            goto fail;
        }
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = p->src;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = p->sink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    if (outputs->name == nullptr || inputs->name == nullptr)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = avfilter_graph_parse_ptr(p->graph, filter_spec, &inputs, &outputs, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: filter parse failed (%s): %s", filter_spec, av_err(ret).c_str());
        goto fail;
    }
    ret = avfilter_graph_config(p->graph, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: filter config failed (%s): %s", filter_spec, av_err(ret).c_str());
        goto fail;
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return 0;

fail:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return ret;
}

int encode_write(AVFormatContext *ofmt, stream_pipe *p, int flush)
{
    AVFrame *frame = flush ? nullptr : p->filt_frame;
    int ret = 0;

    if (frame != nullptr)
    {
        if (p->type == AVMEDIA_TYPE_AUDIO)
        {
            frame->pts = p->next_pts;
            p->next_pts += frame->nb_samples > 0 ? frame->nb_samples : 1;
        }
        else
        {
            frame->pts = p->next_pts;
            p->next_pts += 1;
        }
        frame->time_base = p->enc->time_base;
    }

    ret = avcodec_send_frame(p->enc, frame);
    if (ret < 0)
    {
        log_error("vidwizard: send_frame: %s", av_err(ret).c_str());
        return ret;
    }

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(p->enc, p->enc_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return 0;
        }
        if (ret < 0)
        {
            log_error("vidwizard: receive_packet: %s", av_err(ret).c_str());
            return ret;
        }
        p->enc_pkt->stream_index = p->out_index;
        av_packet_rescale_ts(p->enc_pkt, p->enc->time_base, ofmt->streams[p->out_index]->time_base);
        ret = av_interleaved_write_frame(ofmt, p->enc_pkt);
        av_packet_unref(p->enc_pkt);
        if (ret < 0)
        {
            log_error("vidwizard: write_frame: %s", av_err(ret).c_str());
            return ret;
        }
    }
    return 0;
}

int filter_encode(AVFormatContext *ofmt, stream_pipe *p, AVFrame *frame)
{
    int ret = av_buffersrc_add_frame_flags(p->src, frame, 0);
    if (ret < 0)
    {
        log_error("vidwizard: buffersrc: %s", av_err(ret).c_str());
        return ret;
    }
    while (true)
    {
        ret = av_buffersink_get_frame(p->sink, p->filt_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return 0;
        }
        if (ret < 0)
        {
            log_error("vidwizard: buffersink: %s", av_err(ret).c_str());
            return ret;
        }
        p->filt_frame->time_base = av_buffersink_get_time_base(p->sink);
        p->filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write(ofmt, p, 0);
        av_frame_unref(p->filt_frame);
        if (ret < 0)
        {
            return ret;
        }
    }
}

int open_video_encoder(stream_pipe *p, AVFormatContext *ofmt, unsigned jobs, int width, int height)
{
    const AVCodec *enc = avcodec_find_encoder_by_name("libx264");
    if (enc == nullptr)
    {
        enc = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (enc == nullptr)
    {
        log_error("vidwizard: libx264 encoder not found");
        return AVERROR_ENCODER_NOT_FOUND;
    }
    p->enc = avcodec_alloc_context3(enc);
    if (p->enc == nullptr)
    {
        return AVERROR(ENOMEM);
    }
    p->enc->width = width;
    p->enc->height = height;
    p->enc->pix_fmt = AV_PIX_FMT_YUV420P;
    p->enc->sample_aspect_ratio = p->dec->sample_aspect_ratio;
    AVRational fr = p->dec->framerate;
    if (fr.num <= 0 || fr.den <= 0)
    {
        fr = av_make_q(25, 1);
    }
    p->enc->framerate = fr;
    p->enc->time_base = av_inv_q(fr);
    p->enc->gop_size = 48;
    p->enc->max_b_frames = 2;
    p->enc->thread_count = static_cast<int>(jobs);
    p->enc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (ofmt->oformat->flags & AVFMT_GLOBALHEADER)
    {
        p->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    av_opt_set(p->enc->priv_data, "preset", "medium", 0);
    av_opt_set(p->enc->priv_data, "crf", "16", 0);
    int ret = avcodec_open2(p->enc, enc, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: open libx264: %s", av_err(ret).c_str());
        return ret;
    }
    return 0;
}

int open_audio_encoder(stream_pipe *p, AVFormatContext *ofmt, unsigned jobs)
{
    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (enc == nullptr)
    {
        log_error("vidwizard: AAC encoder not found");
        return AVERROR_ENCODER_NOT_FOUND;
    }
    p->enc = avcodec_alloc_context3(enc);
    if (p->enc == nullptr)
    {
        return AVERROR(ENOMEM);
    }
    p->enc->sample_rate = p->dec->sample_rate;
    int ret = av_channel_layout_copy(&p->enc->ch_layout, &p->dec->ch_layout);
    if (ret < 0)
    {
        return ret;
    }
    p->enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    p->enc->bit_rate = 192000;
    p->enc->time_base = av_make_q(1, p->enc->sample_rate);
    p->enc->thread_count = static_cast<int>(jobs);
    if (ofmt->oformat->flags & AVFMT_GLOBALHEADER)
    {
        p->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    ret = avcodec_open2(p->enc, enc, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: open aac: %s", av_err(ret).c_str());
        return ret;
    }
    return 0;
}

} // namespace

std::string av_error_string(int err)
{
    return av_err(err);
}

int resolve_crop(vw_crop *crop, int src_w, int src_h)
{
    if (crop == nullptr || src_w < 2 || src_h < 2)
    {
        return -1;
    }
    if (crop->centered != 0)
    {
        crop->x = (src_w - crop->width) / 2;
        crop->y = (src_h - crop->height) / 2;
        crop->centered = 0;
    }
    crop->width &= ~1;
    crop->height &= ~1;
    crop->x &= ~1;
    crop->y &= ~1;
    if (crop->width < 2 || crop->height < 2)
    {
        return -1;
    }
    if (crop->x < 0 || crop->y < 0 || crop->x + crop->width > src_w ||
        crop->y + crop->height > src_h)
    {
        log_error("vidwizard: crop %dx%d+%d+%d does not fit in %dx%d", crop->width, crop->height,
                  crop->x, crop->y, src_w, src_h);
        return -1;
    }
    return 0;
}

int transcode_file(const std::filesystem::path &input, const std::filesystem::path &output,
                   const cli_options &opt, const time_window &window, unsigned jobs)
{
    AVFormatContext *ifmt = nullptr;
    AVFormatContext *ofmt = nullptr;
    stream_pipe video{};
    stream_pipe audio{};
    AVPacket *pkt = nullptr;
    int ret = 0;
    int vidx = -1;
    int aidx = -1;
    vw_crop crop{};
    const vw_crop *crop_ptr = nullptr;
    int enc_w = 0;
    int enc_h = 0;
    double dur = 0.0;

    if (ensure_parent_directory(output) != 0)
    {
        return 1;
    }

    ret = avformat_open_input(&ifmt, input.c_str(), nullptr, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: cannot open %s: %s", input.c_str(), av_err(ret).c_str());
        return 1;
    }
    ret = avformat_find_stream_info(ifmt, nullptr);
    if (ret < 0)
    {
        log_error("vidwizard: stream info %s: %s", input.c_str(), av_err(ret).c_str());
        avformat_close_input(&ifmt);
        return 1;
    }

    vidx = av_find_best_stream(ifmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vidx < 0)
    {
        log_error("vidwizard: no video stream in %s", input.c_str());
        avformat_close_input(&ifmt);
        return 1;
    }
    ret = open_decoder(ifmt, vidx, jobs, &video);
    if (ret < 0)
    {
        goto fail;
    }

    aidx = av_find_best_stream(ifmt, AVMEDIA_TYPE_AUDIO, -1, vidx, nullptr, 0);
    if (aidx >= 0)
    {
        ret = open_decoder(ifmt, aidx, jobs, &audio);
        if (ret < 0)
        {
            log_error("vidwizard: warning: audio decoder failed; continuing without audio");
            free_pipe(&audio);
            audio = stream_pipe{};
            aidx = -1;
        }
    }

    enc_w = video.dec->width;
    enc_h = video.dec->height;
    dur = duration_seconds(ifmt, ifmt->streams[vidx]);
    if (opt.crop.has_value())
    {
        crop = *opt.crop;
        if (resolve_crop(&crop, enc_w, enc_h) != 0)
        {
            ret = AVERROR(EINVAL);
            goto fail;
        }
        crop_ptr = &crop;
        if (opt.crop_ranges.empty())
        {
            enc_w = crop.width;
            enc_h = crop.height;
        }
    }

    ret = avformat_alloc_output_context2(&ofmt, nullptr, nullptr, output.c_str());
    if (ret < 0 || ofmt == nullptr)
    {
        log_error("vidwizard: cannot create output %s", output.c_str());
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    {
        AVStream *os = avformat_new_stream(ofmt, nullptr);
        if (os == nullptr)
        {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ret = open_video_encoder(&video, ofmt, jobs, enc_w, enc_h);
        if (ret < 0)
        {
            goto fail;
        }
        ret = avcodec_parameters_from_context(os->codecpar, video.enc);
        if (ret < 0)
        {
            goto fail;
        }
        os->time_base = video.enc->time_base;
        video.out_index = 0;
    }

    if (aidx >= 0)
    {
        const filter_graphs preview =
            build_filter_graphs(opt, window, video.dec->width, video.dec->height, dur, crop_ptr,
                                video.dec->framerate.num, video.dec->framerate.den);
        if (!preview.drop_audio)
        {
            AVStream *os = avformat_new_stream(ofmt, nullptr);
            if (os == nullptr)
            {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            ret = open_audio_encoder(&audio, ofmt, jobs);
            if (ret < 0)
            {
                goto fail;
            }
            ret = avcodec_parameters_from_context(os->codecpar, audio.enc);
            if (ret < 0)
            {
                goto fail;
            }
            os->time_base = audio.enc->time_base;
            audio.out_index = 1;
        }
        else
        {
            free_pipe(&audio);
            audio = stream_pipe{};
            aidx = -1;
        }
    }

    {
        const filter_graphs graphs =
            build_filter_graphs(opt, window, video.dec->width, video.dec->height, dur, crop_ptr,
                                video.dec->framerate.num, video.dec->framerate.den);
        log_verbose("vidwizard: video filter: %s", graphs.video.c_str());
        ret = init_filter(&video, graphs.video.c_str(), jobs);
        if (ret < 0)
        {
            goto fail;
        }
        if (aidx >= 0 && audio.enc != nullptr)
        {
            log_verbose("vidwizard: audio filter: %s", graphs.audio.c_str());
            const char *aspec = graphs.audio.empty() ? "anull" : graphs.audio.c_str();
            ret = init_filter(&audio, aspec, jobs);
            if (ret < 0)
            {
                goto fail;
            }
        }
    }

    if (!(ofmt->oformat->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt->pb, output.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            log_error("vidwizard: cannot write %s: %s", output.c_str(), av_err(ret).c_str());
            goto fail;
        }
    }

    {
        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "movflags", "+faststart", 0);
        ret = avformat_write_header(ofmt, &opts);
        av_dict_free(&opts);
        if (ret < 0)
        {
            log_error("vidwizard: write_header: %s", av_err(ret).c_str());
            goto fail;
        }
    }

    if (window.enabled)
    {
        int64_t ts = static_cast<int64_t>((window.start_s - 0.5) * static_cast<double>(AV_TIME_BASE));
        if (ts < 0)
        {
            ts = 0;
        }
        av_seek_frame(ifmt, -1, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(video.dec);
        if (audio.dec != nullptr)
        {
            avcodec_flush_buffers(audio.dec);
        }
    }

    pkt = av_packet_alloc();
    if (pkt == nullptr)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    while (av_read_frame(ifmt, pkt) >= 0)
    {
        stream_pipe *pipe = nullptr;
        const AVStream *in_st = ifmt->streams[pkt->stream_index];
        if (pkt->stream_index == video.in_index)
        {
            pipe = &video;
        }
        else if (audio.dec != nullptr && pkt->stream_index == audio.in_index)
        {
            pipe = &audio;
        }
        else
        {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(pipe->dec, pkt);
        av_packet_unref(pkt);
        if (ret < 0)
        {
            log_error("vidwizard: send_packet: %s", av_err(ret).c_str());
            goto fail;
        }
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(pipe->dec, pipe->dec_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            if (ret < 0)
            {
                goto fail;
            }
            pipe->dec_frame->pts = pipe->dec_frame->best_effort_timestamp;
            if (window.enabled)
            {
                const double t = packet_time_s(in_st, pipe->dec_frame->best_effort_timestamp);
                if (t < window.start_s)
                {
                    av_frame_unref(pipe->dec_frame);
                    continue;
                }
                if (t >= window.end_s)
                {
                    pipe->past_window = true;
                    av_frame_unref(pipe->dec_frame);
                    continue;
                }
            }
            ret = filter_encode(ofmt, pipe, pipe->dec_frame);
            av_frame_unref(pipe->dec_frame);
            if (ret < 0)
            {
                goto fail;
            }
        }
        if (window.enabled && video.past_window && (audio.dec == nullptr || audio.past_window))
        {
            break;
        }
    }

    /* Flush decoders and filters. */
    {
        stream_pipe *pipes[2] = {&video, audio.dec != nullptr ? &audio : nullptr};
        for (int i = 0; i < 2; i++)
        {
            stream_pipe *pipe = pipes[i];
            if (pipe == nullptr || pipe->dec == nullptr)
            {
                continue;
            }
            avcodec_send_packet(pipe->dec, nullptr);
            while (avcodec_receive_frame(pipe->dec, pipe->dec_frame) >= 0)
            {
                pipe->dec_frame->pts = pipe->dec_frame->best_effort_timestamp;
                if (window.enabled)
                {
                    const AVStream *in_st = ifmt->streams[pipe->in_index];
                    const double t = packet_time_s(in_st, pipe->dec_frame->best_effort_timestamp);
                    if (t < window.start_s || t >= window.end_s)
                    {
                        av_frame_unref(pipe->dec_frame);
                        continue;
                    }
                }
                ret = filter_encode(ofmt, pipe, pipe->dec_frame);
                av_frame_unref(pipe->dec_frame);
                if (ret < 0)
                {
                    goto fail;
                }
            }
            ret = filter_encode(ofmt, pipe, nullptr);
            if (ret < 0)
            {
                goto fail;
            }
            ret = encode_write(ofmt, pipe, 1);
            if (ret < 0)
            {
                goto fail;
            }
        }
    }

    ret = av_write_trailer(ofmt);
    if (ret < 0)
    {
        log_error("vidwizard: trailer: %s", av_err(ret).c_str());
        goto fail;
    }

    av_packet_free(&pkt);
    free_pipe(&video);
    free_pipe(&audio);
    avformat_close_input(&ifmt);
    if (ofmt != nullptr && !(ofmt->oformat->flags & AVFMT_NOFILE) && ofmt->pb != nullptr)
    {
        avio_closep(&ofmt->pb);
    }
    avformat_free_context(ofmt);
    return 0;

fail:
    av_packet_free(&pkt);
    free_pipe(&video);
    free_pipe(&audio);
    if (ifmt != nullptr)
    {
        avformat_close_input(&ifmt);
    }
    if (ofmt != nullptr)
    {
        if (!(ofmt->oformat->flags & AVFMT_NOFILE) && ofmt->pb != nullptr)
        {
            avio_closep(&ofmt->pb);
        }
        avformat_free_context(ofmt);
    }
    return 1;
}

} // namespace vidwizard
