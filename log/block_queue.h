#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include "../locker/locker.h"

template <typename T>
class block_queue {
   public:
    block_queue(int max_size = 1000) {
        if (max_size < 0) {
            exit(-1);
        }
        m_max_size = max_size;
        m_size = 0;
        m_array = new T[m_max_size];
        m_front = -1;
        m_back = -1;
        // pthread_cond_init(&m_cond, NULL);
        // pthread_mutex_init(&m_lock, NULL);
    }
    ~block_queue();
    void clear();
    bool push(T item);
    bool pop(T& item);
    bool full();
    bool empty();
    int size();
    bool front(T& item);
    bool back(T& item);
    int max_size();

   private:
    int m_max_size;  // 阻塞队列的最大容量
    int m_size;      // 队列当前的大小
    T* m_array;      // 队列
    int m_front;     //队头坐标
    int m_back;      // 队尾坐标
    // pthread_cond_t m_cond;
    // pthread_mutex_t m_lock;
    // 封装一下，利用构造函数的特性直接完成初始化
    cond m_cond;    // 信号量
    locker m_lock;  // 互斥锁
};

// 析构函数
template <typename T>
block_queue<T>::~block_queue() {
    m_lock.lock();
    if (m_array != NULL)
        delete[] m_array;
    m_lock.unlock();
}

// 清空队列
template <typename T>
void block_queue<T>::clear() {
    m_lock.lock();
    m_size = 0;
    m_front = -1;
    m_back = -1;
    m_lock.unlock();
}

// 添加元素
template <typename T>
bool block_queue<T>::push(T item) {
    m_lock.lock();
    if (m_size == m_max_size) {
        m_cond.broadcast();
        m_lock.unlock();
        return false;
    }
    m_back = (m_back + 1) % m_max_size;
    m_array[m_back] = item;
    m_size++;
    m_cond.broadcast();
    m_lock.unlock();
    return true;
}

// 删除元素
template <typename T>
bool block_queue<T>::pop(T& item) {
    m_lock.lock();
    if (m_size == 0) {
        m_lock.unlock();
        return false;
    }
    m_front = (m_front + 1) % m_max_size;
    item = m_array[m_front];
    m_size--;
    m_lock.unlock();
    return true;
}

// 检查队列是否已满
template <typename T>
bool block_queue<T>::full() {
    int tmp;
    m_lock.lock();
    tmp = m_size;
    m_lock.unlock();
    return tmp == m_max_size;
}

// 检查是否为空
template <typename T>
bool block_queue<T>::empty() {
    int tmp;
    m_lock.lock();
    tmp = m_size;
    m_lock.unlock();
    return tmp == 0;
}

// 查询队列大小
template <typename T>
int block_queue<T>::size() {
    int len;
    m_lock.lock();
    len = m_size;
    m_lock.unlock();
    return len;
}

// 查询队列的最大容量
template <typename T>
int block_queue<T>::max_size() {
    int maxLen;
    m_lock.lock();
    maxLen = m_max_size;
    m_lock.unlock();
    return maxLen;
}

// 返回队头
template <typename T>
bool block_queue<T>::front(T& item) {
    m_lock.lock();
    // while (m_size <= 0) {
    //     if (!m_cond.wait(m_lock.get())) {
    //         m_lock.unlock();
    //         return false;
    //     }
    // }
    if (m_size == 0) {
        m_lock.unlock();
        return false;
    }
    item = m_array[(m_front + 1) % m_max_size];
    m_lock.unlock();
    return true;
}

// 返回队尾
template <typename T>
bool block_queue<T>::back(T& item) {
    m_lock.lock();
    if (m_size == 0) {
        m_lock.unlock();
        return false;
    }
    item = m_array[m_back];
    m_lock.unlock();
    return true;
}

#endif