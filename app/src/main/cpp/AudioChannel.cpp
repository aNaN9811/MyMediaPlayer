#include "AudioChannel.h"

AudioChannel::AudioChannel(int stream_index, AVCodecContext *codecContext, AVRational time_base)
        : BaseChannel(stream_index, codecContext, time_base) {
    out_channels = av_get_channel_layout_nb_channels(
            AV_CH_LAYOUT_STEREO); // STEREO:双声道类型 == 获取 声道数 2
    out_sample_size = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16); // 每个sample是16 bit == 2字节
    out_sample_rate = 44100; // 采样率

    out_buffers_size = out_sample_rate * out_sample_size * out_channels;
    out_buffers = static_cast<uint8_t *>(malloc(out_buffers_size));

    swr_ctx = swr_alloc_set_opts(nullptr,
                                 AV_CH_LAYOUT_STEREO,  // 声道布局类型 双声道
                                 AV_SAMPLE_FMT_S16,  // 采样大小 16bit
                                 out_sample_rate, // 采样率  44100
                                 codecContext->channel_layout, // 声道布局类型
                                 codecContext->sample_fmt, // 采样大小
                                 codecContext->sample_rate,  // 采样率
                                 0, 0);
    swr_init(swr_ctx);
}

AudioChannel::~AudioChannel() {
}

void *task_audio_decode(void *args) {
    auto *audio_channel = static_cast<AudioChannel *>(args);
    audio_channel->audio_decode();
    return nullptr;
}

void *task_audio_play(void *args) {
    auto *audio_channel = static_cast<AudioChannel *>(args);
    audio_channel->audio_play();
    return nullptr;
}

// 音频：1.解码，2.播放
void AudioChannel::start() {
    isPlaying = true;

    packets.setWork(1);
    frames.setWork(1);

    // 第一个线程： 取出队列的压缩包进行解码，解码后的原始包再push到播放队列中去 （音频：PCM数据）
    pthread_create(&pid_audio_decode, nullptr, task_audio_decode, this);

    // 第二线线程：从播放队列取出原始包，播放  音频播放OpenSLES
    pthread_create(&pid_audio_play, nullptr, task_audio_play, this);
}

void AudioChannel::stop() {
    isPlaying = false;
    pthread_detach(pid_audio_decode);
    pthread_detach(pid_audio_play);
}

// 第一个线程： 取出队列的压缩包进行解码，解码后的原始包再push到播放队列中去 （音频：PCM数据）
void AudioChannel::audio_decode() {
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

        // 1.发送pkt（压缩包）给缓冲区
        ret = avcodec_send_packet(codecContext, pkt);
        if (ret) {
            break;
        }

        // 2.从缓冲区取出来（原始包）
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

void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *args) {

    auto *audio_channel = static_cast<AudioChannel *>(args);

    int pcm_size = audio_channel->getPCM();

    (*bq)->Enqueue(
            bq,
            audio_channel->out_buffers, // PCM数据
            pcm_size);
}

int AudioChannel::getPCM() {
    int pcm_data_size = 0;

    AVFrame *frame = nullptr;
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

        // 重采样
        int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, frame->sample_rate) +
                                            frame->nb_samples, // 获取下一个输入样本相对于下一个输出样本将经历的延迟
                                            out_sample_rate, // 输出采样率
                                            frame->sample_rate, // 输入采样率
                                            AV_ROUND_UP);
        int samples_per_channel = swr_convert(swr_ctx,
                // 下面是输出区域
                                              &out_buffers,  // 【成果的buff】  重采样后的
                                              dst_nb_samples, // 【成果的 单通道的样本数 无法与out_buffers对应，所以有下面的pcm_data_size计算】

                // 下面是输入区域
                                              (const uint8_t **) frame->data, // 队列的AVFrame *PCM数据 未重采样的
                                              frame->nb_samples); // 输入的样本数

        // 由于out_buffers 和 dst_nb_samples 无法对应，所以需要重新计算
        pcm_data_size = samples_per_channel * out_sample_size *
                        out_channels;
        audio_time = frame->best_effort_timestamp * av_q2d(time_base);
        if (this->jniCallbackHelper) {
            jniCallbackHelper->onProgress(THREAD_CHILD, audio_time);
        }
        break;
    }
    releaseAVFrame(&frame);
    return pcm_data_size;
}

// 第二线线程：从播放队列取出原始包，播放  音频播放OpenSLES
void AudioChannel::audio_play() {
    SLresult result;

    // 1.创建引擎对象并获取【引擎接口】
    // 1.1 创建引擎对象：SLObjectItf engineObject
    result = slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("创建引擎 slCreateEngine error");
        return;
    }
    // 1.2 初始化引擎
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE); // SL_BOOLEAN_FALSE:延时等待创建成功
    if (SL_RESULT_SUCCESS != result) {
        LOGE("创建引擎 Realize error");
        return;
    }
    // 1.3 获取引擎接口
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineInterface);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("创建引擎接口 Realize error");
        return;
    }
    if (engineInterface) {
        LOGD("创建引擎接口 create success");
    } else {
        LOGE("创建引擎接口 create error");
        return;
    }

    // 2.设置混音器
    // 2.1 创建混音器
    result = (*engineInterface)->CreateOutputMix(engineInterface, &outputMixObject, 0, nullptr,
                                                 nullptr);
    if (SL_RESULT_SUCCESS != result) {
        LOGD("初始化混音器 CreateOutputMix failed");
        return;
    }
    // 2.2 初始化混音器
    result = (*outputMixObject)->Realize(outputMixObject,
                                         SL_BOOLEAN_FALSE); // SL_BOOLEAN_FALSE:延时等待你创建成功
    if (SL_RESULT_SUCCESS != result) {
        LOGD("初始化混音器 (*outputMixObject)->Realize failed");
        return;
    }
    // 不启用混响可以不用获取混音器接口 【声音的效果】
    // 获得混音器接口
    /*
    result = (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB,
                                             &outputMixEnvironmentalReverb);
    if (SL_RESULT_SUCCESS == result) {
    // 设置混响 ： 默认。
    SL_I3DL2_ENVIRONMENT_PRESET_ROOM: 室内
    SL_I3DL2_ENVIRONMENT_PRESET_AUDITORIUM : 礼堂 等
    const SLEnvironmentalReverbSettings settings = SL_I3DL2_ENVIRONMENT_PRESET_DEFAULT;
    (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
           outputMixEnvironmentalReverb, &settings);
    }
    */
    LOGD("2、设置混音器 Success");

    // 3.创建播放器
    // 3.1 创建buffer缓存类型的队列大小
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
                                                       10};
    SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, // PCM数据格式
                                   2, // 声道数
                                   SL_SAMPLINGRATE_44_1, // 采样率 44100
                                   SL_PCMSAMPLEFORMAT_FIXED_16, // 每秒采样样本 16bit
                                   SL_PCMSAMPLEFORMAT_FIXED_16, // 每个样本位数 16bit
                                   SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, // 前左声道  前右声道
                                   SL_BYTEORDER_LITTLEENDIAN}; // 字节序(小端) 例如：int类型四个字节（到底是 高位在前 还是 低位在前 的排序方式，一般我们都是小端）
    // 数据源 audioSrc 最终配置音频信息的成果，给后面代码使用
    SLDataSource audioSrc = {&loc_bufq, &format_pcm};
    // 3.2 配置音轨（输出）
    // 设置混音器
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX,
                                          outputMixObject}; // SL_DATALOCATOR_OUTPUTMIX:输出混音器类型
    SLDataSink audioSnk = {&loc_outmix, nullptr}; // outmix最终混音器的成果，给后面代码使用
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    // 3.3 创建播放器 SLObjectItf bqPlayerObject
    result = (*engineInterface)->CreateAudioPlayer(engineInterface, // 参数1：引擎接口
                                                   &bqPlayerObject, // 参数2：播放器
                                                   &audioSrc, // 参数3：音频配置信息
                                                   &audioSnk, // 参数4：混音器
                                                   1, // 参数5：开放的参数的个数
                                                   ids,  // 参数6：代表我们需要 Buff
                                                   req // 参数7：代表我们上面的Buff 需要开放出去
    );
    if (SL_RESULT_SUCCESS != result) {
        LOGE("创建播放器 CreateAudioPlayer failed!");
        return;
    }
    // 3.4 初始化播放器：SLObjectItf bqPlayerObject
    result = (*bqPlayerObject)->Realize(bqPlayerObject,
                                        SL_BOOLEAN_FALSE);  // SL_BOOLEAN_FALSE:延时等待你创建成功
    if (SL_RESULT_SUCCESS != result) {
        LOGE("实例化播放器 CreateAudioPlayer failed!");
        return;
    }
    LOGD("创建播放器 CreateAudioPlayer success!");
    // 3.5 获取播放器接口 【以后播放全部使用 播放器接口去干（核心）】
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY,
                                             &bqPlayerPlay); // SL_IID_PLAY:播放接口 == iplayer
    if (SL_RESULT_SUCCESS != result) {
        LOGD("获取播放接口 GetInterface SL_IID_PLAY failed!");
        return;
    }
    LOGI("3、创建播放器 Success");


    // 4.设置回调函数
    // 4.1 获取播放器队列接口：SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue 播放需要的队列
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
                                             &bqPlayerBufferQueue);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("获取播放队列 GetInterface SL_IID_BUFFERQUEUE failed!");
        return;
    }
    // 4.2 设置回调 void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
    (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue,  // 传入刚刚设置好的队列
                                             bqPlayerCallback,  // 回调函数
                                             this); // 给回调函数的参数
    LOGD("4、设置播放回调函数 Success");

    // 5、设置播放器状态为播放状态
    (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
    LOGD("5、设置播放器状态为播放状态 Success");

    // 6、手动激活回调函数
    bqPlayerCallback(bqPlayerBufferQueue, this);
    LOGD("6、手动激活回调函数 Success");
}
