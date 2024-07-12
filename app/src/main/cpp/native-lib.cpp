#include "MyPlayer.h"
#include <android/native_window_jni.h>

extern "C" {
#include <libavutil/avutil.h>
}

MyPlayer *player = nullptr;
JavaVM *vm = nullptr;
ANativeWindow *window = nullptr;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

jint JNI_OnLoad(JavaVM *vm, void *args) {
    ::vm = vm;
    return JNI_VERSION_1_6;
}

// 函数指针实现 渲染工作
void renderFrame(uint8_t *src_data, int width, int height, int src_lineSize) {
    pthread_mutex_lock(&mutex);
    if (!window) {
        pthread_mutex_unlock(&mutex);
        return;
    }

    // 设置窗口的大小，各个属性
    if (ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888) != 0) {
        ANativeWindow_release(window);
        window = nullptr;
        pthread_mutex_unlock(&mutex);
        return;
    }
    ANativeWindow_Buffer window_buffer;
    if (ANativeWindow_lock(window, &window_buffer, nullptr)) {
        ANativeWindow_release(window);
        window = nullptr;
        pthread_mutex_unlock(&mutex);
        return;
    }

    // 开始渲染 rgba数据 -> 字节对齐，填充 window_buffer 即填充画面
    auto *dst_data = static_cast<uint8_t *>(window_buffer.bits);
    int dst_linesize = window_buffer.stride * 4;
    for (int i = 0; i < window_buffer.height; ++i) {
        memcpy(dst_data + i * dst_linesize, src_data + i * src_lineSize, width * 4);
    }

    // 解锁并且刷新 window_buffer 数据显示的画面
    ANativeWindow_unlockAndPost(window);

    pthread_mutex_unlock(&mutex);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_mymediaplayer_MyPlayer_prepareNative(JNIEnv *env, jobject job,
                                                      jstring data_source) {
    const char *data_source_ = env->GetStringUTFChars(data_source, nullptr);
    auto *helper = new JNICallbackHelper(vm, env, job);
    player = new MyPlayer(data_source_, helper);
    player->setRenderCallback(renderFrame);
    player->prepare();
    env->ReleaseStringUTFChars(data_source, data_source_);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_mymediaplayer_MyPlayer_startNative(JNIEnv *env, jobject thiz) {
    if (player) {
        player->start();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_mymediaplayer_MyPlayer_restartNative(JNIEnv *env, jobject thiz) {
    if (player) {
        player->restart();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_mymediaplayer_MyPlayer_stopNative(JNIEnv *env, jobject thiz) {
    if (player) {
        player->stop();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_mymediaplayer_MyPlayer_releaseNative(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&mutex);

    if (player) {
        player->release();
    }

    // 先释放之前的显示窗口
    if (window) {
        ANativeWindow_release(window);
        window = nullptr;
    }

    pthread_mutex_unlock(&mutex);

    DELETE(player);
    DELETE(vm);
    DELETE(window);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_mymediaplayer_MyPlayer_setSurfaceNative(JNIEnv *env, jobject thiz,
                                                         jobject surface) {
    pthread_mutex_lock(&mutex);
    if (window) {
        ANativeWindow_release(window);
        window = nullptr;
    }
    window = ANativeWindow_fromSurface(env, surface);
    pthread_mutex_unlock(&mutex);
}

extern "C"
JNIEXPORT int JNICALL
Java_com_example_mymediaplayer_MyPlayer_getDurationNative(JNIEnv *env, jobject thiz) {
    if (player) {
        return player->getDuration();
    }
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_mymediaplayer_MyPlayer_seekNative(JNIEnv *env, jobject thiz, jint play_progress) {
    if (player) {
        player->seek(play_progress);
    }
}