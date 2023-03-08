#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <iostream>
#include <unistd.h>
#include "./http/http_conn.h"
#include "./Connection_pool/connectionPool.h"
#include "./locker/locker.h"
#include "./log/log.h"
#include "./threadpool/threadpool.h"
#include "./timer/timer.h"

#define MAX_USERS 65535  // 最大的接入用户个数，也即是最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 最大可处理的任务数量
#define TIMESLOT 5              // 最小超时单位

#define SYNLOG  // 同步写日志
// #define ASYNLOG  // 异步写日志

// 添加文件描述符到epoll对象中
extern void addfd(int epollfd, int fd, bool one_shot);

// 从epoll对象中删除文件描述符
extern void delfd(int epollfd, int fd);

// 修改文件描述符，重制socket上的EPOLLONESHOT事件，已确保下一次可读时，EPOLLIN事件能被触发
extern void modfd(int epollfd, int fd, int modev);

extern int setnonblocking(int fd);

// 设置定时器相关参数
static int pipefd[2];               // 负责读写
static sort_timer_list timer_list;  // 升序定时器列表
static int epollfd = 0;

// 信号处理函数
void sig_handler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    // 可重入性表示中断后再次进入该函数，环境变量与之前相同，不会丢失数据
    int old_errno = errno;
    int msg = sig;
    // 将信号值从管道写端写入，传输字符类型，而非整型
    // 信号最大到64，一字节以内
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = old_errno;
}

// 给信号更改新的处理方式
void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;  // 设置处理函数
    if (restart)
        sa.sa_flags |= SA_RESTART;
    // 设置临时阻塞信号集，执行信号处理函数时屏蔽所有信号
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler() {
    timer_list.tick();
    alarm(TIMESLOT);
}

//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data* user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
}

int main(int argc, char* argv[]) {
#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0);  //同步日志模型
#endif

#ifdef ASYNLOG
    Log::get_instance()->init("./serverLog/serverLog.txt", 1000, 20000, 8);
#endif
    if (argc <= 1) {
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    LOG_INFO("%s", "The server starts working");

    //  获取端口号
    int port = atoi(argv[1]);  // argv[0]是程序名称

    // 更改对SIGPIPE信号的处理方式
    addsig(SIGPIPE, SIG_IGN);

    // 创建数据库连接池
    ConnectionPool* conn_pool = ConnectionPool::get_pool();
    // 测试代码
    // string sql(
    //     "INSERT INTO user (name, age, sex) VALUES ('Test_user2', 1, "
    //     "'female');");
    // std::shared_ptr<Connection> p = conn_pool->get_connection();
    // p->update(sql);

    // 创建线程池，初始化线程池
    threadpool<http_conn>* pool = nullptr;  // 一开始设置为nullptr
    // 尝试创建线程池
    try {
        pool = new threadpool<http_conn>;
    } catch (...) {
        LOG_INFO("%s", "服务器线程池创建失败");
        return -1;
    }

    LOG_INFO("%s", "服务器线程池创建完成");

    // 创建保存客户端连接信息的数组
    http_conn* users = new http_conn[MAX_USERS];

    // 读取用户名和密码，进行缓存
    users->init_mysql_result(conn_pool);
    // 测试代码，看一下能否正确读取
    // for (auto& kV : users->user_info) {
    //     string name = kV.first;
    //     string password = kV.second;
    //     cout << name << " " << password << endl;
    // }

    // 创建监听端口套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    // 设置端口复用，要在绑定之前设置
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 绑定
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
    int ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));

    // 监听
    // 当套接字正在处理客户端请求时，如果有新的请求进来，套接字是没法处理的，只能把它放进缓冲区，待当前请求处理完毕后，再从缓冲区中读取出来处理。如果不断有新的请求进来，它们就按照先后顺序在缓冲区中排队，直到缓冲区满。这个缓冲区，就称为请求队列，这里请求队列长度设置为5
    // 要注意监听只是关注端口是否有连接到来，如果要连接客户端需要使用accept
    ret = listen(listenfd, 5);

    // 创建epoll对象，事件数组，添加文件描述符
    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);

    // 将监听文件描述符添加到epoll对象中
    // 所有线程都可以操作这个端口，因此不需要oneshot
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    // 创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    // 设置写端非阻塞
    // 这是因为如果send函数在发送时如果缓存已满会阻塞等待，而定时函数执行的需求等级比较低，为节省信号处理函数的执行时间，因此设置为非阻塞
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);

    client_data* users_timers = new client_data[MAX_USERS];

    bool stop_server = false;

    // 超时标志
    bool timeout = false;
    alarm(TIMESLOT);

    LOG_INFO("%s", "服务器开始监听");
    Log::get_instance()->flush();

    while (!stop_server) {
        // 等待监控文件描述符上有事件的产生
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && (errno != EINTR)) {
            printf("Epoll failed\n");
            break;
        }
        for (int i = 0; i < number; ++i) {
            int sockfd = events[i].data.fd;

            if (sockfd == listenfd) {
                // 说明有新的客户端请求连接，需要建立新连接
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int connfd = accept(sockfd, (struct sockaddr*)&client_addr,
                                    &client_addr_len);
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_USERS) {
                    // 目前连接数满了
                    // TODO：给客户端写一个信息：服务器内部正忙
                    close(connfd);
                    continue;
                }
                // 没有异常，将新连接添加到连接数组中
                // 因为按顺序从前到后操作不方便，就用文件描述符直接作为索引
                users[connfd].init(connfd, client_addr);

                // 初始化client_data数据
                // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                users_timers[connfd].address = client_addr;
                users_timers[connfd].sockfd = connfd;
                // 初始的到时时间是当前的时间+三倍的TIMESLOT
                // 回调函数设置为cb_func，遇到信号仅通过管道传递信号值，具体业务逻辑由主线程完成
                time_t t = time(nullptr) + TIMESLOT * 3;
                m_timer* timer = new m_timer(t, cb_func, &users_timers[connfd]);
                users_timers[connfd].timer = timer;
                timer_list.add_timer(timer);
            } else if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
                // 服务器端断开连接，响应定时器关闭
                // users[sockfd].close_conn();

                // 关闭定时器
                m_timer* timer = users_timers[sockfd].timer;
                timer->cb_func(&users_timers[sockfd]);
                if (timer) {
                    timer_list.del_timer(timer);
                }
            } else if (sockfd == pipefd[0] && (events[i].events & EPOLLIN)) {
                // 处理信号
                int sig;
                char signals[1024];
                ret = recv(sockfd, signals, sizeof(signals), 0);
                if (ret < 0) {
                    // 传输错误，处理错误;
                    continue;
                } else if (ret == 0) {
                    // 传输正确，对端的socket已关闭
                    continue;
                } else {
                    for (int i = 0; i < ret; ++i) {
                        switch (signals[i]) {
                            case SIGALRM: {
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {
                                stop_server = true;
                            }
                        }
                    }
                }
            } else if (events[i].events & EPOLLIN) {
                // 有新的活动，重置定时器
                m_timer* timer = users_timers[sockfd].timer;
                // 检测到读事件，将该事件放入到请求队列里面
                if (users[sockfd].read_once()) {
                    pool->append(users + sockfd);
                    // 有数据传输，将该定时器往后移动3个单位
                    // 并调整定时器在双向链表中的位置
                    if (timer) {
                        timer->expire = time(nullptr) + 3 * TIMESLOT;
                        timer_list.mod_timer(timer);
                        // printf("调整一次定时器\n");
                    }
                } else {
                    // 关闭连接，删除定时器
                    // users[sockfd].close_conn();
                    timer->cb_func(&users_timers[sockfd]);
                    if (timer) {
                        timer_list.del_timer(timer);
                    }
                }
            } else if (events[i].events & EPOLLOUT) {
                // 遇到读事件，一样需要重置相应定时器
                m_timer* timer = users_timers[sockfd].timer;
                // 同上，需要一次性写出
                if (users[sockfd].write_once()) {
                    if (timer) {
                        timer->expire = time(nullptr) + 3 * TIMESLOT;
                        timer_list.mod_timer(timer);
                    }
                } else {
                    // 关闭连接，删除定时器
                    // users[sockfd].close_conn();
                    timer->cb_func(&users_timers[sockfd]);
                    if (timer) {
                        timer_list.del_timer(timer);
                    }
                }
            }
        }
        if (timeout) {
            timer_handler();
            timeout = false;
        }
    }

    // 关闭占用的文件描述符
    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete pool;
    delete[] users;
    delete[] users_timers;

    return 0;
}