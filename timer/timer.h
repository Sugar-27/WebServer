#ifndef TIMER_H
#define TIMER_H

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

class m_timer;

// 存储客户信息
struct client_data {
    sockaddr_in address;
    int sockfd;
    m_timer* timer;
};

// 定时器类
class m_timer {
   public:
    m_timer() : pre(NULL), next(NULL) {}

   public:
    m_timer* pre;
    m_timer* next;
    client_data* user_data;
    time_t expire;  // 到期时间
    void (*cb_func)(client_data*);
};

// 升序定时器双向链表，用于保存定时器
class sort_timer_list {
   public:
    sort_timer_list();
    ~sort_timer_list();
    void add_timer(m_timer* timer);
    void mod_timer(m_timer* timer);
    void del_timer(m_timer* timer);
    void tick();

   private:
    void add_timer(m_timer* timer, m_timer* start);
    m_timer *head, *tail;
};

#endif