#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>
#include <exception>

/* 
    线程同步机制封装类
    一共封装了三个类，分别包括：互斥量、条件变量、信号量
    1. 互斥量保证不同线程不会进入同一段代码
    2. 条件变量用于某个线程需要在某种条件成立时才去保护它将要操作的临界区，这种情况从而避免了线程不断轮询检查该条件是否成立而降低效率的情况，这是实现了效率提高。在条件满足时，自动退出阻塞，再加锁进行操作
    3. 信号量用来保证两个或多个关键代码段不被并发调用
*/

class locker {
   public:
    locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    ~locker() { pthread_mutex_destroy(&m_mutex); }
    bool lock() { return pthread_mutex_lock(&m_mutex) == 0; }
    bool unlock() { return pthread_mutex_unlock(&m_mutex) == 0; }
    pthread_mutex_t* get() { return &m_mutex; }

   private:
    pthread_mutex_t m_mutex;
};

class cond {
   public:
    cond() {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    ~cond() { pthread_cond_destroy(&m_cond); }
    bool wait(pthread_mutex_t* m_mutex) {
        return pthread_cond_wait(&m_cond, m_mutex) == 0;
    }
    bool timedwait(pthread_mutex_t* m_mutex, struct timespec t) {
        return pthread_cond_timedwait(&m_cond, m_mutex, &t);
    }
    bool signal() { return pthread_cond_signal(&m_cond) == 0; }
    bool broadcast() { return pthread_cond_broadcast(&m_cond); }

   private:
    pthread_cond_t m_cond;
};

class sem {
   public:
    sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(unsigned int val) {
        if (sem_init(&m_sem, 0, val) != 0) {
            throw std::exception();
        }
    }
    ~sem() { sem_destroy(&m_sem); }
    bool post() {
        // 信号量+1
        return sem_post(&m_sem) == 0;
    }
    bool wait() {
        // 信号量-1，如果为0则阻塞等待信号量大于零
        return sem_wait(&m_sem) == 0;
    }

   private:
    sem_t m_sem;
};

#endif