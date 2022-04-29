#ifndef HTTP_CONN_H
#define HTTP_CONN_H

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
#include <unistd.h>
#include <memory>
#include <string>
#include <unordered_map>
#include "../Connection_pool/connectionPool.h"

using std::string;

// 设置文件描述符非阻塞
int setnonblocking(int fd);

// 添加文件描述符到epoll对象中
void addfd(int epollfd, int fd, bool one_shot);

// 从epoll对象中删除文件描述符
void delfd(int epollfd, int fd);

// 修改文件描述符，重制socket上的EPOLLONESHOT事件，已确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int modev);

class http_conn {
   public:
    // 定义一些状态
    // 定义HTTP请求方法，目前只支持GET
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    /*
         服务器处理HTTP请求的可能结果，报文解析的结果
         NO_REQUEST          :   请求不完整，需要继续读取客户数据
         GET_REQUEST         :   表示获得了一个完成的客户请求
         BAD_REQUEST         :   表示客户请求语法错误
         NO_RESOURCE         :   表示服务器没有资源
         FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
         FILE_REQUEST        :   文件请求,获取文件成功
         INTERNAL_ERROR      :   表示服务器内部错误
         CLOSED_CONNECTION   :   表示客户端已经关闭连接了
     */
    enum HTTP_CODE {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 0.读取到一个完整的行 1.行出错 2.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

    // 数据库用的哈希表，用来存用户和密码
    static std::unordered_map<string, string> user_info;

   public:
    // 所有的连接共享同一个epoll对象，也就是将所有socket上的事件都注册到同一个epoll内核事件中
    static int m_epollfd;
    // 统计用户数量
    static int m_user_count;
    // 读缓冲与写缓冲区大小设定
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    // 文件名的最大长度
    static const int FILENAME_LEN = 200;

    http_conn(){};
    ~http_conn(){};
    void process();                                  // 处理客户端请求
    void init(int connfd, const sockaddr_in& addr);  // 初始化新接收的连接
    void close_conn();                               // 关闭连接
    bool read_once();                                // 一次性读入
    bool write_once();                               // 一次性写出
    static void init_mysql_result(
        ConnectionPool* conn_pool);  // 将数据库的用户名和密码读到内存里

   private:
    /* data */
    int m_sockfd;                          // 该HTTP连接的socket套接字
    sockaddr_in m_address;                 // 通信的socket地址
    char read_buffer[READ_BUFFER_SIZE];    // 读缓冲区
    char write_buffer[WRITE_BUFFER_SIZE];  // 写缓冲区
    int m_write_idx;   // 写缓冲区中待发送的字节数
    int m_read_idx;    // 记录下一次读时开始坐标
    int m_check_idx;   // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;  // 当前正在解析的行的起始位置
    CHECK_STATE m_check_state;  // 当前状态机所处状态
    char* m_url;                // 请求目标的文件地址
    char* m_version;            // HTTP版本
    METHOD m_method;            // 请求方法
    char* m_host;               // 主机名
    int cgi;                    // 是否启用cgi
    char* m_string;             // 保存post报文
    bool m_iflink;              // HTTP请求是否保持连接
    int m_content_length;       // 请求报文的请求体的长度
    // 客户请求的目标文件完整路径，内容等于doc_root+m_url
    char m_real_file[FILENAME_LEN];
    // 资源状态，用来判断文件是否存在、是否为目录、是否可读以及文件的大小信息
    struct stat m_file_stat;
    char* m_file_address;  // 客户请求的目标文件被映射到内存中
    int bytes_to_send;     // 将要发送的字节数
    int bytes_have_send;   //已经发送的字节数
    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    struct iovec m_iv[2];
    int m_iv_count;
    // 保护哈希表
    locker m_lock;

   private:
    /* function */
    HTTP_CODE parse_read();                    // 解析请求
    HTTP_CODE parse_request_line(char* text);  // 解析请求行
    HTTP_CODE parse_headers(char* text);       // 解析请求头
    HTTP_CODE parse_content(char* text);       // 解析请求体
    LINE_STATUS parse_line();                  // 解析一行
    bool process_write(HTTP_CODE ret);         // 填充HTTP应答
    void init();  // 初始化除连接以外的所有信息
    char* get_line() { return read_buffer + m_start_line; }
    HTTP_CODE do_request();
    void unmap();  // 解除映射

    bool add_status_line(int status, const char* title);  // 返回响应
    bool add_response(const char* format, ...);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_content(const char* content);
    bool add_content_type();
    bool add_linger();
    bool add_blank_line();
};

#endif