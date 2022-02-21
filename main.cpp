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
#include <unistd.h>
#include "./http/http_conn.h"
#include "./locker/locker.h"
#include "./threadpool/threadpool.h"

#define MAX_USERS 65535  // 最大的接入用户个数，也即是最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 最大可处理的任务数量

// 添加文件描述符到epoll对象中
extern void addfd(int epollfd, int fd, bool one_shot);

// 从epoll对象中删除文件描述符
extern void delfd(int epollfd, int fd);

// 修改文件描述符，重制socket上的EPOLLONESHOT事件，已确保下一次可读时，EPOLLIN事件能被触发
extern void modfd(int epollfd, int fd, int modev);

// 给信号更改新的处理方式
void addsig(int sig, void(handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }
    //  获取端口号
    int port = atoi(argv[1]);  // argv[0]是程序名称

    // 更改对SIGPIPE信号的处理方式
    addsig(SIGPIPE, SIG_IGN);

    // 创建线程池，初始化线程池
    threadpool<http_conn>* pool = NULL;  // 一开始设置为NULL
    // 尝试创建线程池
    try {
        pool = new threadpool<http_conn>;
    } catch (...) {
        return -1;
    }

    // 创建保存客户端连接信息的数组
    http_conn* users = new http_conn[MAX_USERS];

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
    //当套接字正在处理客户端请求时，如果有新的请求进来，套接字是没法处理的，只能把它放进缓冲区，待当前请求处理完毕后，再从缓冲区中读取出来处理。如果不断有新的请求进来，它们就按照先后顺序在缓冲区中排队，直到缓冲区满。这个缓冲区，就称为请求队列，这里请求队列长度设置为5
    // 要注意监听只是关注端口是否有连接到来，如果要连接客户端需要使用accept
    ret = listen(listenfd, 5);

    // 创建epoll对象，事件数组，添加文件描述符
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);

    // 将监听文件描述符添加到epoll对象中
    // 所有线程都可以操作这个端口，因此不需要oneshot
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (true) {
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
                    // 给客户端写一个信息：服务器内部正忙（未完成）
                    close(connfd);
                    continue;
                }
                // 没有异常，将新连接添加到连接数组中
                // 因为按顺序从前到后操作不方便，就用文件描述符直接作为索引
                users[connfd].init(connfd, client_addr);
            } else if (events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) {
                // 由于是边缘触发模式，因此需要一次性把数据读出来
                if (users[sockfd].read_once()) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                // 同上，需要一次性写出
                if (!users[sockfd].write_once()) {
                    users[sockfd].close_conn();
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete pool;
    delete[] users;

    return 0;
}