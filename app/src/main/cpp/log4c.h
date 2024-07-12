#ifndef MYMEDIAPLAYER_LOG4C_H
#define MYMEDIAPLAYER_LOG4C_H

#include <android/log.h>

#define TAG "MyMediaPlayerNative"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG,  __VA_ARGS__);
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG,  __VA_ARGS__);
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG,  __VA_ARGS__);

#endif
