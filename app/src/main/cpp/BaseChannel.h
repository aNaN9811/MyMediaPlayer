#ifndef MYMEDIAPLAYER_BASECHANNEL_H
#define MYMEDIAPLAYER_BASECHANNEL_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
};

#include "safe_queue.h"
#include "log4c.h"
#include "JNICallbackHelper.h"

class BaseChannel {

public:
    int stream_index;
    SafeQueue<AVPacket *> packets; // 压缩数据包队列
    SafeQueue<AVFrame *> frames; // 原始数据包队列
    bool isPlaying; // 是否播放
    AVCodecContext *codecContext = nullptr; // 解码器上下文
    AVRational time_base;
    JNICallbackHelper *jniCallbackHelper = nullptr;

    BaseChannel(int stream_index, AVCodecContext *codecContext, AVRational time_base)
            :
            stream_index(stream_index),
            codecContext(codecContext),
            time_base(time_base) {
        packets.setReleaseCallback(releaseAVPacket);
        frames.setReleaseCallback(releaseAVFrame);
    }

    virtual ~BaseChannel() {
        packets.clear();
        frames.clear();
    }

    void setJNICallbackHelper(JNICallbackHelper *jniCallbackHelper) {
        this->jniCallbackHelper = jniCallbackHelper;
    }

    /**
     * 释放 队列中 所有的 AVPacket *
     * @param packet
     */
    static void releaseAVPacket(AVPacket **p) {
        if (p) {
            av_packet_free(p);
            *p = nullptr;
        }
    }

    /**
     * 释放 队列中 所有的 AVFrame *
     * @param packet
     */
    static void releaseAVFrame(AVFrame **f) {
        if (f) {
            av_frame_free(f);
            *f = nullptr;
        }
    }
};

#endif
