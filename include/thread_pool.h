//
// Created by INDEX on 2023/2/24.
//

#ifndef WEBSERVER_THREAD_POOL_H
#define WEBSERVER_THREAD_POOL_H

#include<pthread.h>
#include<list>
#include<locker.h>

// 模板类，代码复用
template<typename T> // 任务类型
class ThreadPool {
public:
    ThreadPool(int thread_num = 8, int max_requests = 10000); // 默认构造函数

    ~ThreadPool();

    bool append(T *request);

private:
    static void *Worker(void *arg); // 线程工作函数

    void Run(); // 启动线程

private:
    int m_thread_num;

    pthread_t *m_threads; // 线程列表

    int m_max_requests; // 请求队列最大长度

    std::list<T> m_request_queue; // 请求队列

    Locker m_queue_locker;

    Sem m_queue_sem;

    bool m_stop; // 是否结束线程
};

#endif //WEBSERVER_THREAD_POOL_H
