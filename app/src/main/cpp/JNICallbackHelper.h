#ifndef MYMEDIAPLAYER_JNICALLBAKCHELPER_H
#define MYMEDIAPLAYER_JNICALLBAKCHELPER_H

#include <jni.h>
#include "util.h"

class JNICallbackHelper {

private:
    JavaVM *vm = nullptr;
    JNIEnv *env = nullptr;
    jobject job;
    jmethodID jmd_prepared;
    jmethodID jmd_onError;
    jmethodID jmd_onProgress;

public:
    JNICallbackHelper(JavaVM *vm, JNIEnv *env, jobject job);

    virtual ~JNICallbackHelper();

    void onPrepared(int thread_mode);

    void onError(int thread_mode, int error_code);

    void onProgress(int thread_mode, int audio_time);
};


#endif
