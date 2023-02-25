//
// Created by INDEX on 2023/2/24.
//

#include "thread_pool.h"

template<typename T>
ThreadPool<T>::ThreadPool(int thread_num, int max_requests):
        m_thread_num(thread_num),
        m_max_requests(max_requests),
        m_stop(false), m_threads(nullptr) {

    if ((m_thread_num <= 0 || m_max_requests <= 0)) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_num];

    for (int i = 0; i < m_thread_num; i++) {
        printf("create the %d thread\n", i);

        if (pthread_create(m_threads + i, nullptr, Worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i]) != 0) { // 设置为detach模式，系统自动回收资源
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool ThreadPool<T>::Append(T *request) {
    m_queue_locker.Lock();
    if (m_request_queue.size() > m_max_requests) {
        m_queue_locker.Unlock();
        return false;
    }

    m_request_queue.push_back(request);
    m_queue_locker.Unlock();
    m_queue_sem.Post();
    return true;
}

template<typename T>
void *ThreadPool<T>::Worker(void *arg) { // 因为Worker必须是静态函数（pthread的要求），所以该函数属于类，不属于某个对象实例，所以不能直接通过this指针修改实例中的值
    auto *thread_pool = (ThreadPool<T> *) arg;

    thread_pool->m_stop = true;
    thread_pool->m_stop = false;
    thread_pool->Run(); // 如果想通过该静态函数修改类中的非静态对象，只需传入一个实例的this指针，并通过该指针调用对象的非静态成员函数即可
    return thread_pool;
}

template<typename T>
void ThreadPool<T>::Run() {
    while (!m_stop) {
        m_queue_sem.Wait();
        m_queue_locker.Lock();
        if (m_request_queue.empty()) {
            m_queue_locker.Unlock();
            continue;
        }

        T *request = m_request_queue.front(); // 获取工作
        m_request_queue.pop_front();
        m_queue_locker.Unlock();

        if (!request) {
            continue; // 没有工作可以做
        }

        request->Process();
    }
}

template
class ThreadPool<HttpConn>;