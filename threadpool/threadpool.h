#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stdio.h>
#include <list>
#include "../locker/locker.h"
#include "../log/log.h"

template <typename T>
class threadpool {
   public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int threadnumber = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);

   private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void* worker(void* arg);
    void run();
    int m_threadnumber;   // 线程池中的线程数量
    int m_max_requests;   // 请求队列中允许处理的最大任务数
    pthread_t* m_thread;  // 描述线程池的数组，大小为m_threadnumber
    std::list<T*> m_worklist;  // 工作请求队列，被所有线程共享，因此需要线程同步
    locker m_listlocker;  // 保护请求队列的互斥锁
    sem m_liststate;      // 是否有任务需要处理
    bool m_stop;          // 是否结束线程
};

template <typename T>
threadpool<T>::threadpool(int threadnumber, int max_requests)
    : m_threadnumber(threadnumber),
      m_max_requests(max_requests),
      m_thread(nullptr),
      m_stop(false) {
    if (m_threadnumber < 0 || m_max_requests < 0) {
        throw std::exception();
    }
    // 线程池建立，线程池中共有m_threadnumber个线程
    m_thread = new pthread_t[m_threadnumber];
    if (!m_thread) {
        throw std::exception();
    }
    // 创建线程，填满线程池
    for (int i = 0; i < m_threadnumber; ++i) {
        if (pthread_create(m_thread + i, nullptr, worker, this) != 0) {
            delete[] m_thread;
            throw std::exception();
        }
        // 设置线程分离，使得线程的结束由系统接管
        if (pthread_detach(m_thread[i]) != 0) {
            delete[] m_thread;
            throw std::exception();
        }
        printf("create %dst thread\n", i);
        Log::get_instance()->flush();
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_thread;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T* request) {
    m_listlocker.lock();
    if (m_worklist.size() > m_max_requests) {
        m_listlocker.unlock();
        return false;
    }
    // 在工作队列中添加任务
    m_worklist.push_back(request);
    m_listlocker.unlock();
    m_liststate.post();  // 给定+1信号，说明又有一个新任务需要处理
    return true;
}

template <typename T>
void* threadpool<T>::worker(void* arg) {
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    // 开始处理任务
    while (!m_stop) {
        // 先wait再lock，防止阻塞时上锁
        m_liststate.wait();
        m_listlocker.lock();
        if (m_worklist.empty()) {
            m_listlocker.unlock();
            continue;
        }
        T* task = m_worklist.front();
        m_worklist.pop_front();
        m_listlocker.unlock();
        if (!task) {
            continue;
        }
        task->process();
    }
}

#endif