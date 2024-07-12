#ifndef MYMEDIAPLAYER_MYPLAYER_H
#define MYMEDIAPLAYER_MYPLAYER_H

#include <cstring>
#include <pthread.h>
#include "AudioChannel.h"
#include "VideoChannel.h"
#include "JNICallbackHelper.h"
#include "util.h"

extern "C" {
#include <libavformat/avformat.h>
}

class MyPlayer {
private:
    char *data_source = nullptr;
    pthread_t pid_prepare;
    pthread_t pid_start;
    pthread_t pid_release;
    pthread_mutex_t seek_mutex;
    AVFormatContext *formatContext = nullptr;
    AudioChannel *audio_channel = nullptr;
    VideoChannel *video_channel = nullptr;
    JNICallbackHelper *helper = nullptr;
    RenderCallback renderCallback;
    bool isPlaying;
    int duration;

public:
    MyPlayer(const char *data_source, JNICallbackHelper *helper);

    ~MyPlayer();

    void prepare();

    void prepare_();

    void restart();

    void start();

    void start_();

    void stop();

    void release_(MyPlayer *);

    void release();

    int getDuration() const;

    void seek(int play_value);

    void setRenderCallback(RenderCallback renderCallback);
};


#endif
