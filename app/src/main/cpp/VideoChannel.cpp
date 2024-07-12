#include "VideoChannel.h"

void dropAVFrame(queue<AVFrame *> &q) {
    if (!q.empty()) {
        AVFrame *frame = q.front();
        BaseChannel::releaseAVFrame(&frame);
        q.pop();
    }
}

void dropAVPacket(queue<AVPacket *> &q) {
    while (!q.empty()) {
        AVPacket *pkt = q.front();
        if (pkt->flags != AV_PKT_FLAG_KEY) { // 非关键帧，可以丢弃
            BaseChannel::releaseAVPacket(&pkt);
            q.pop();
        } else {
            break; // 如果是关键帧，不能丢，那就结束
        }
    }
}

VideoChannel::VideoChannel(int stream_index, AVCodecContext *codecContext, AVRational time_base,
                           int fps)
        : BaseChannel(stream_index, codecContext, time_base), fps(fps) {
    frames.setSyncCallback(dropAVFrame);
    packets.setSyncCallback(dropAVPacket);
}

VideoChannel::~VideoChannel() {
}

void *task_video_decode(void *args) {
    auto *video_channel = static_cast<VideoChannel *>(args);
    video_channel->video_decode();
    return nullptr;
}

void *task_video_play(void *args) {
    auto *video_channel = static_cast<VideoChannel *>(args);
    video_channel->video_play();
    return nullptr;
}

// 视频：1.解码，2.播放
void VideoChannel::start() {
    isPlaying = true;

    packets.setWork(1);
    frames.setWork(1);

    // 第一个线程： 视频：取出队列的压缩包 进行解码 解码后的原始包 再push队列中去（视频：YUV）
    pthread_create(&pid_video_decode, nullptr, task_video_decode, this);

    // 第二线线程：视频：从队列取出原始包，播放
    pthread_create(&pid_video_play, nullptr, task_video_play, this);
}

void VideoChannel::stop() {
    isPlaying = false;
    pthread_detach(pid_video_decode);
    pthread_detach(pid_video_play);
}

// 1.取出队列的压缩包 进行解码 解码后的原始包 再push队列中去（视频：YUV）
void VideoChannel::video_decode() {
    AVPacket *pkt = nullptr;
    while (isPlaying) {
        if (!frames.getWork()) {
            continue;
        }
        if (frames.size() > 100) {
            av_usleep(10 * 1000); // 单位 ：microseconds 微妙 10毫秒
            continue;
        }

        int ret = packets.getQueueAndDel(pkt);
        if (!isPlaying) {
            break;
        }
        if (!ret) {
            continue;
        }
        ret = avcodec_send_packet(codecContext, pkt);
        if (ret) {
            break;
        }
        AVFrame *frame = av_frame_alloc();
        ret = avcodec_receive_frame(codecContext, frame);
        if (ret == AVERROR(EAGAIN)) {
            if (frame) {
                releaseAVFrame(&frame);
            }
            continue;
        } else if (ret != 0) {
            if (frame) {
                releaseAVFrame(&frame);
            }
            break;
        }
        frames.insertToQueue(frame);
        releaseAVPacket(&pkt);
    }
    if (pkt) {
        releaseAVPacket(&pkt);
    }
}

// 2.从队列取出原始包，播放
void VideoChannel::video_play() {
    AVFrame *frame = nullptr;
    uint8_t *dst_data[4]; // RGBA
    int dst_linesize[4]; // RGBA

    // 原始包（YUV数据 -> [libswscale] Android屏幕（RGBA数据）
    // 给 dst_data 申请内存   width * height * 4
    av_image_alloc(dst_data, dst_linesize,
                   codecContext->width, codecContext->height, AV_PIX_FMT_RGBA, 1);
    SwsContext *sws_ctx = sws_getContext(
            // 下面是输入环节
            codecContext->width,
            codecContext->height,
            codecContext->pix_fmt, // 自动获取 xxx.mp4 的像素格式 AV_PIX_FMT_YUV420P 写死的
            // 下面是输出环节
            codecContext->width,
            codecContext->height,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
    while (isPlaying) {
        if (!frames.getWork()) {
            continue;
        }
        int ret = frames.getQueueAndDel(frame);
        if (!isPlaying) {
            break;
        }
        if (!ret) {
            continue;
        }
        // 格式转换 yuv -> rgba
        sws_scale(sws_ctx,
                // 下面是输入环节 YUV的数据
                  frame->data, frame->linesize,
                  0, codecContext->height,
                // 下面是输出环节  成果：RGBA数据
                  dst_data,
                  dst_linesize
        );

        // extra_delay = repeat_pict / (2*fps)
        double extra_delay = frame->repeat_pict / (2 * fps); // 在之前的编码时，加入的额外延时时间取出来（可能获取不到）
        double fps_delay = 1.0 / fps; // 根据fps得到延时时间
        double real_delay = fps_delay + extra_delay;
        double video_time = frame->best_effort_timestamp * av_q2d(time_base);
        double audio_time = audio_channel->audio_time;
        double time_diff = video_time - audio_time;

        if (time_diff > 0) {
            if (time_diff > 1) {
                av_usleep((real_delay * 2) * 1000000);
            } else { // 说明：0~1之间：音频与视频差距不大，所以可以那（当前帧实际延时时间 + 音视频差值）
                av_usleep((real_delay + time_diff) * 1000000); // 单位是微妙：所以 * 1000000
            }
        } else if (time_diff < 0) {
            int count = fabs(time_diff) / fps_delay;
            frames.sync(count);
            continue;
        }

        renderCallback(dst_data[0], codecContext->width, codecContext->height, dst_linesize[0]);
        releaseAVFrame(&frame);
    }
    if (frame) {
        releaseAVFrame(&frame);
    }
    isPlaying = false;
    av_free(&dst_data[0]);
    sws_freeContext(sws_ctx);
}

void VideoChannel::setRenderCallback(RenderCallback renderCallback) {
    this->renderCallback = renderCallback;
}

void VideoChannel::setAudioChannel(AudioChannel *audio_channel) {
    this->audio_channel = audio_channel;
}
