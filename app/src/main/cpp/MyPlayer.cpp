#include "MyPlayer.h"

MyPlayer::MyPlayer(const char *data_source, JNICallbackHelper *helper) {
    this->data_source = new char[strlen(data_source) + 1];
    strcpy(this->data_source, data_source);
    this->helper = helper;
}

MyPlayer::~MyPlayer() {
    if (data_source) {
        delete data_source;
        data_source = nullptr;
    }

    if (helper) {
        delete helper;
        helper = nullptr;
    }
}

void *task_prepare(void *args) {
    auto *player = static_cast<MyPlayer *>(args);
    player->prepare_();
    return nullptr;
}

void MyPlayer::prepare_() {

    // 1，打开媒体地址（文件路径， 直播地址rtmp）
    formatContext = avformat_alloc_context();
    AVDictionary *dictionary = nullptr;
    av_dict_set(&dictionary, "timeout", "5000000", 0); // 单位微秒
    /**
     * 1，AVFormatContext *
     * 2，路径
     * 3，AVInputFormat *fmt  Mac、Windows 摄像头、麦克风
     * 4，各种设置：例如：Http 连接超时， 打开rtmp的超时  AVDictionary **options
     */
    int r = avformat_open_input(&formatContext, data_source, nullptr, &dictionary);
    av_dict_free(&dictionary);
    if (r) {
        if (helper) {
            helper->onError(THREAD_CHILD, FFMPEG_CAN_NOT_OPEN_URL);
            // char * errorInfo = av_err2str(r); // 根据返回值 得到错误详情
        }
        return;
    }

    // 2，查找媒体中的音视频流的信息
    r = avformat_find_stream_info(formatContext, nullptr);
    if (r < 0) {
        if (helper) {
            helper->onError(THREAD_CHILD, FFMPEG_CAN_NOT_FIND_STREAMS);
        }
        return;
    }

    // 3，根据流信息，流的个数，用循环来找
    for (int i = 0; i < formatContext->nb_streams; ++i) {
        // 4，获取媒体流（视频，音频）
        AVStream *stream = formatContext->streams[i];

        // 5，从上面的流中 获取 编码解码的【参数】
        AVCodecParameters *parameters = stream->codecpar;

        // 6，根据上面的【参数】获取编解码器
        auto *codec = const_cast<AVCodec *>(avcodec_find_decoder(parameters->codec_id));
        if (!codec) {
            if (helper) {
                helper->onError(THREAD_CHILD, FFMPEG_FIND_DECODER_FAIL);
            }
            return;
        }

        // 7，编解码器 上下文 （这个才是真正干活的）
        AVCodecContext *codecContext = avcodec_alloc_context3(codec);
        if (!codecContext) {
            if (helper) {
                helper->onError(THREAD_CHILD, FFMPEG_ALLOC_CODEC_CONTEXT_FAIL);
            }
            return;
        }

        // 8，avcodec parameters to context
        r = avcodec_parameters_to_context(codecContext, parameters);
        if (r < 0) {
            if (helper) {
                helper->onError(THREAD_CHILD, FFMPEG_CODEC_CONTEXT_PARAMETERS_FAIL);
            }
            return;
        }

        // 9，打开解码器
        r = avcodec_open2(codecContext, codec, nullptr);
        if (r) {
            if (helper) {
                helper->onError(THREAD_CHILD, FFMPEG_OPEN_DECODER_FAIL);
            }
            return;
        }

        AVRational time_base = stream->time_base;
        this->duration = formatContext->duration / AV_TIME_BASE;

        // 10，从编解码器参数中，获取流的类型 codec_type  ===  音频 视频
        if (parameters->codec_type == AVMediaType::AVMEDIA_TYPE_AUDIO) { // 音频
            audio_channel = new AudioChannel(i, codecContext, time_base);
            if (this->duration != 0) { // 非直播，才有意义把 JNICallbackHelper传递过去
                audio_channel->setJNICallbackHelper(helper);
            }
        } else if (parameters->codec_type == AVMediaType::AVMEDIA_TYPE_VIDEO) { // 视频
            if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                continue;
            }
            AVRational fps_rational = stream->avg_frame_rate;
            int fps = av_q2d(fps_rational);
            video_channel = new VideoChannel(i, codecContext, time_base, fps);
            video_channel->setRenderCallback(renderCallback);
            if (this->duration != 0) { // 非直播，才有意义把 JNICallbackHelper传递过去
                video_channel->setJNICallbackHelper(helper);
            }
        }
    } // for end

    // 11，判断是否有音视频流
    if (!audio_channel && !video_channel) {
        if (helper) {
            helper->onError(THREAD_CHILD, FFMPEG_NOMEDIA);
        }
        return;
    }

    // 12，媒体文件准备成功，通知上层
    if (helper) {
        helper->onPrepared(THREAD_CHILD);
    }
}

void MyPlayer::prepare() {
    pthread_create(&pid_prepare, nullptr, task_prepare, this);
    pthread_mutex_init(&seek_mutex, nullptr);
}

void *task_start(void *args) {
    auto *player = static_cast<MyPlayer *>(args);
    player->start_();
    return nullptr;
}

void MyPlayer::start_() {
    while (isPlaying) {
        if (video_channel && !video_channel->packets.getWork()) {
            continue;
        }
        if (audio_channel && !audio_channel->packets.getWork()) {
            continue;
        }
        if (video_channel && video_channel->packets.size() > 100) {
            av_usleep(10 * 1000); // 单位 ：microseconds 微妙 10毫秒
            continue;
        }
        if (audio_channel && audio_channel->packets.size() > 100) {
            av_usleep(10 * 1000);
            continue;
        }

        // AVPacket 可能是音频 也可能是视频（压缩包）
        AVPacket *packet = av_packet_alloc();
        int ret = av_read_frame(formatContext, packet);
        if (!ret) {
            if (video_channel && video_channel->stream_index == packet->stream_index) {
                video_channel->packets.insertToQueue(packet);
            } else if (audio_channel && audio_channel->stream_index == packet->stream_index) {
                audio_channel->packets.insertToQueue(packet);
            }
        } else if (ret == AVERROR_EOF) { //  end of file 读到文件末尾了
            if (video_channel->packets.empty() && audio_channel->packets.empty()) {
                break; // 队列的数据被音频 视频 全部播放完毕才退出
            }
        } else {
            break; // av_read_frame 出现了错误，结束当前循环
        }
    } // end while
    isPlaying = false;
    video_channel->stop();
    audio_channel->stop();
}

void MyPlayer::start() {
    isPlaying = true;

    // 1.解码 2.播放
    if (video_channel) {
        video_channel->setAudioChannel(audio_channel);
        video_channel->start();
    }
    if (audio_channel) {
        audio_channel->start();
    }

    pthread_create(&pid_start, nullptr, task_start, this);
}

void MyPlayer::restart() {
    if (audio_channel) {
        audio_channel->packets.setWork(1);
        audio_channel->frames.setWork(1);
    }
    if (video_channel) {
        video_channel->packets.setWork(1);
        video_channel->frames.setWork(1);
    }
}

void MyPlayer::setRenderCallback(RenderCallback renderCallback) {
    this->renderCallback = renderCallback;
}

int MyPlayer::getDuration() const {
    return duration; // 在调用此函数之前，必须给此duration变量赋值
}

void MyPlayer::seek(int progress) {
    if (progress < 0 || progress > duration) {
        return;
    }
    if (!audio_channel && !video_channel) {
        return;
    }
    if (!formatContext) {
        return;
    }
    pthread_mutex_lock(&seek_mutex);
    // 1.formatContext 安全问题
    // 2.-1 代表默认情况，FFmpeg自动选择 音频 还是 视频 做 seek
    // 3. AVSEEK_FLAG_ANY 直接精准到 拖动的位置，问题：如果不是关键帧，B帧 可能会造成 花屏情况
    //    AVSEEK_FLAG_BACKWARD（则优）
    //    AVSEEK_FLAG_FRAME 找关键帧（非常不准确，可能会跳的太多），一般不会直接用，但是会配合用
    int r = av_seek_frame(formatContext, -1, progress * AV_TIME_BASE, AVSEEK_FLAG_FRAME);
    if (r < 0) {
        return;
    }
    if (audio_channel) {
        audio_channel->packets.setWork(0);
        audio_channel->frames.setWork(0);
        audio_channel->packets.clear();
        audio_channel->frames.clear();
        audio_channel->packets.setWork(1);
        audio_channel->frames.setWork(1);
    }
    if (video_channel) {
        video_channel->packets.setWork(0);
        video_channel->frames.setWork(0);
        video_channel->packets.clear();
        video_channel->frames.clear();
        video_channel->packets.setWork(1);
        video_channel->frames.setWork(1);
    }
    pthread_mutex_unlock(&seek_mutex);
}

void MyPlayer::stop() {
    if (audio_channel) {
        audio_channel->packets.setWork(0);
        audio_channel->frames.setWork(0);
    }
    if (video_channel) {
        video_channel->packets.setWork(0);
        video_channel->frames.setWork(0);
    }
}

void *task_release(void *args) {
    auto *player = static_cast<MyPlayer *>(args);
    player->release_(player);
    return nullptr;
}

void MyPlayer::release_(MyPlayer *derryPlayer) {
    isPlaying = false;
    pthread_mutex_destroy(&seek_mutex);
    pthread_detach(pid_prepare);
    pthread_detach(pid_start);
    if (formatContext) {
        avformat_close_input(&formatContext);
        avformat_free_context(formatContext);
        formatContext = nullptr;
    }
    DELETE(audio_channel);
    DELETE(video_channel);
    DELETE(derryPlayer);
}

void MyPlayer::release() {
    helper = nullptr;
    if (audio_channel) {
        audio_channel->jniCallbackHelper = nullptr;
    }
    if (video_channel) {
        video_channel->jniCallbackHelper = nullptr;
    }
    pthread_create(&pid_release, nullptr, task_release, this);
}