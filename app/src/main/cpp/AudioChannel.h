#ifndef MYMEDIAPLAYER_AUDIOCHANNEL_H
#define MYMEDIAPLAYER_AUDIOCHANNEL_H

#include "BaseChannel.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

extern "C" {
#include <libswresample/swresample.h> // 对音频数据进行转换（重采样）
}

class AudioChannel : public BaseChannel {

private:
    pthread_t pid_audio_decode;
    pthread_t pid_audio_play;

public:
    int out_channels;
    int out_sample_size;
    int out_sample_rate;
    int out_buffers_size;
    uint8_t *out_buffers = nullptr;
    SwrContext *swr_ctx = nullptr;
    double audio_time;

public:
    // 引擎
    SLObjectItf engineObject = nullptr;
    // 引擎接口
    SLEngineItf engineInterface = nullptr;
    // 混音器
    SLObjectItf outputMixObject = nullptr;
    // 播放器
    SLObjectItf bqPlayerObject = nullptr;
    // 播放器接口
    SLPlayItf bqPlayerPlay = nullptr;
    // 播放器队列接口
    SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue = nullptr;

public:
    AudioChannel(int stream_index, AVCodecContext *codecContext, AVRational time_base);

    ~AudioChannel();

    void stop();

    void start();

    void audio_decode();

    void audio_play();

    int getPCM();
};


#endif
