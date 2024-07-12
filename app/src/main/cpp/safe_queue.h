#ifndef MYMEDIAPLAYER_SAFE_QUEUE_H
#define MYMEDIAPLAYER_SAFE_QUEUE_H

#include <queue>
#include <pthread.h>

using namespace std;

template<typename T>
class SafeQueue {

private:
    typedef void (*ReleaseCallback)(T *);

    typedef void (*SyncCallback)(queue<T> &);

    queue<T> queue;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int work;
    ReleaseCallback releaseCallback;
    SyncCallback syncCallback;

public:
    SafeQueue() {
        pthread_mutex_init(&mutex, nullptr);
        pthread_cond_init(&cond, nullptr);
    }

    ~SafeQueue() {
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&cond);
    }

    /**
     * 入队 [ AVPacket *  压缩包]  [ AVFrame * 原始包]
     */
    void insertToQueue(T value) {
        pthread_mutex_lock(&mutex);

        if (work) {
            queue.push(value);
            pthread_cond_signal(&cond);
        } else {
            if (releaseCallback) {
                releaseCallback(&value);
            }
        }

        pthread_mutex_unlock(&mutex);
    }

    /**
     *  出队 [ AVPacket *  压缩包]  [ AVFrame * 原始包]
     */
    int getQueueAndDel(T &value) {
        int ret = 0;

        pthread_mutex_lock(&mutex);

        while (work && queue.empty()) {
            pthread_cond_wait(&cond, &mutex);
        }

        if (!queue.empty()) {
            value = queue.front();
            queue.pop();
            ret = 1;
        }

        pthread_mutex_unlock(&mutex);

        return ret;
    }

    /**
    * 设置工作状态，设置队列是否工作
    * @param work
    */
    void setWork(int work) {
        pthread_mutex_lock(&mutex);

        this->work = work;

        pthread_cond_signal(&cond);// 唤醒阻塞的位置

        pthread_mutex_unlock(&mutex);
    }

    int empty() {
        return queue.empty();
    }

    int size() {
        return queue.size();
    }

    /**
     * 清空队列中所有的数据，循环一个一个的删除
     */
    void clear() {
        pthread_mutex_lock(&mutex);

        unsigned int size = queue.size();

        for (int i = 0; i < size; ++i) {
            T value = queue.front();
            if (releaseCallback) {
                releaseCallback(&value);
            }
            queue.pop();
        }

        pthread_mutex_unlock(&mutex);
    }

    /**
     * 设置此函数指针的回调，让外界去释放
     * @param releaseCallback
     */
    void setReleaseCallback(ReleaseCallback releaseCallback) {
        this->releaseCallback = releaseCallback;
    }

    /**
    * 设置此函数指针的回调，让外界去释放
    * @param syncCallback
    */
    void setSyncCallback(SyncCallback syncCallback) {
        this->syncCallback = syncCallback;
    }

    /**
     * 同步操作 丢包
     */
    void sync(int count) {
        pthread_mutex_lock(&mutex);
        for (int i = 0; i < count; i++) {
            syncCallback(queue);
        }
        pthread_mutex_unlock(&mutex);
    }


    int getWork() {
        return work;
    }
};

#endif
