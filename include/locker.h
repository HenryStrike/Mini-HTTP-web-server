//
// Created by INDEX on 2023/2/24.
//

#ifndef WEBSERVER_LOCKER_H
#define WEBSERVER_LOCKER_H

#include<pthread.h>
#include<exception>
#include<semaphore.h>

// 非模板类直接在h文件中实现，使代码更紧凑
// 线程同步机制封装，使用mutex_lock，命名使用Google C++ style
class Locker {
public:
    Locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }

    ~Locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    bool Lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool Unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    pthread_mutex_t *GetLock() {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

// 条件变量封装
class Cond {
public:
    Cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }

    ~Cond() {
        pthread_cond_destroy(&m_cond);
    }

    bool Wait(pthread_mutex_t *mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0; // 等到lock释放时再唤醒
    }

    bool TimeWait(pthread_mutex_t *mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }

    bool Signal() {
        return pthread_cond_signal(&m_cond) == 0; // 唤醒一个线程
    }

    bool Broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0; // 唤醒所有线程
    }

private:
    pthread_cond_t m_cond;
};


// 信号量封装
class Sem {
public:
    Sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }

    Sem(int num) { // 给定值初始化
        if (sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }

    ~Sem() {
        sem_destroy(&m_sem);
    }

    bool Wait() {
        return sem_wait(&m_sem) == 0;
    }

    bool Post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};


#endif //WEBSERVER_LOCKER_H
