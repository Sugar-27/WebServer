#include "http_conn.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// 网站根目录
const char* doc_root = "/home/zht411/Anaconda/WebServer/webserver";
// const char* doc_root = "/home/sugar/Code/WebServer/webserver";

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form =
    "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form =
    "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form =
    "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form =
    "There was an unusual problem serving the requested file.\n";

// 设置文件描述符非阻塞
int setnonblocking(int fd) {
    int oldOption = fcntl(fd, F_GETFL);
    int newOption = oldOption | O_NONBLOCK;
    fcntl(fd, F_SETFL, newOption);
    return oldOption;
}

// 添加文件描述符到epoll对象中
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLRDHUP; // 水平触发
    if (one_shot) {
        ev.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    // 设置文件描述符非阻塞
    // 因为如果阻塞的话，当没有数据到达时，该文件描述符会阻塞一直等待有数据到达开始处理
    // 非阻塞则没有数据时直接返回
    setnonblocking(fd);
}

// 从epoll对象中删除文件描述符，然后关闭文件描述符
void delfd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重制socket上的EPOLLONESHOT事件，已确保下一次可读时，EPOLLIN事件能被触发
void modfd(int epollfd, int fd, int modev) {
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = modev | EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
}

// 初始化static变量
std::unordered_map<string, string> http_conn::user_info;

// 所有socket上的事件都被注册到同一个epoll内核事件中，所以设置成静态的
int http_conn::m_epollfd = -1;
// 所有的客户数，全部的http_conn共享，因为是总的客户数
int http_conn::m_user_count = 0;

// 由线程池中的线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {
    // 解析HTTP请求
    HTTP_CODE read_ret = parse_read();
    if (read_ret == NO_REQUEST) {
        // NO_REQUEST表示请求不完整，需要继续接收请求
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    // 生成响应
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    // 注册并监听写事件
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

// 初始化连接，外部调用初始化套接字地址
void http_conn::init(int connfd, const sockaddr_in& addr) {
    m_sockfd = connfd;
    m_address = addr;

    // 设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));

    // 添加到epoll内核中，进行监听
    addfd(m_epollfd, connfd, true);
    m_user_count++;

    init();
}

// 初始化新接受的连接
// check_state默认为分析请求行状态
void http_conn::init() {
    m_read_idx = 0; // 读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    m_write_idx = 0;
    m_check_state = CHECK_STATE_REQUESTLINE; // 初始化状态为解析首行
    m_check_idx = 0;  // 当前正在解析的字符在读缓冲区中的位置
    m_start_line = 0; // 当前正在解析的行在读缓冲区中的首位置
    m_method = GET;   // 定义请求方法默认为GET
    m_url = 0;
    m_version = 0;
    m_host = 0;
    m_string = nullptr;
    cgi = 0;
    m_content_length = 0;
    bytes_have_send = 0;
    bytes_have_send = 0;
    m_iflink = false;

    bzero(read_buffer, READ_BUFFER_SIZE);
    bzero(write_buffer, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}

// 关闭连接
void http_conn::close_conn() {
    if (m_sockfd != -1) {
        delfd(m_epollfd, m_sockfd);
        --m_user_count; // 减去关闭的用户数
        m_sockfd = -1;
    }
}

// 一次性读入，循环读取用户数据直到数据末尾或用户断开连接
bool http_conn::read_once() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int read_bytes = 0;
    while (true) {
        // 从套接字里面接收数据，存储在m_read_buf缓冲区中
        read_bytes = recv(m_sockfd, read_buffer + m_read_idx,
                          READ_BUFFER_SIZE - m_read_idx, 0);
        if (read_bytes == -1) {
            // 这两个错误码在Linux下是一个值（在大多数系统里也是一个值）
            // 该错误码在非阻塞情况下表示无数据可读
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有数据
            }
            return false;
        } else if (read_bytes == 0) {
            // 说明对方主动关闭连接
            return false;
        }
        m_read_idx += read_bytes;
    }
    LOG_INFO("读到数据：\n%s\n", read_buffer);
    return true;
}

// 一次性完成HTTP响应
bool http_conn::write_once() {
    int temp = 0;

    if (bytes_to_send == 0) {
        // 将要发送的字节为0，这一次响应结束
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (true) {
        // 分散写
        // 这一步就把数据发出去了
        // m_iv[0]是响应头，即我们的writebuffer
        // m_iv[1]是响应体，即我们映射的内存m_realfile_address
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            // 如果发送失败，但不是缓冲区问题，取消映射
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = write_buffer + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0) {
            // 没有数据要发送了
            unmap();
            // EPOLL树上重置EPOLLONESHOT事件
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_iflink) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }

    return true;
}

// 主状态机，解析请求
http_conn::HTTP_CODE http_conn::parse_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = nullptr;

    // 考虑两种情况，前一个情况解析到了请求体；后一个情况是检验到一行数据;这两种情况都是解析到了完整的数据
    // line_status==LINE_OK是为了防止无限循环，用LINE_OPEN退出循环
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           (line_status = parse_line()) == LINE_OK) {
        // 获取一行数据
        // parse_line将/r/n置为/0/0
        text = get_line();
        // 切换新行首指针（m_start_line - 1 指向的是/0）
        m_start_line = m_check_idx;
        // LOG_INFO("got 1 http line:%s\n", text);
        // 主状态机：CHECK_STATE_REQUESTLINE->CHECK_STATE_HEADER->CHECK_STATE_CONTENT
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                // 解析请求行，获得HTTP连接的请求方法，目标URL，HTTP版本
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                // 解析请求头
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                // 解析请求体
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 解析请求行，获得HTTP连接的请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");

    // 没有空格或者\t则说明报文格式有误
    if (!m_url) {
        return BAD_REQUEST;
    }

    *m_url++ = '\0';     // m_url = "/index.html HTTP/1.1"
    char* method = text; // method = "GET"
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else {
        return BAD_REQUEST;
    }
    // m_version = " HTTP/1.1"(前面有个空格)
    m_version = strpbrk(m_url, " ");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0'; // m_version = "HTTP/1.1"
    // 只支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;                 // m_url = "192.168.0.107:9999/index.html"
        m_url = strchr(m_url, '/'); // m_url = "/index.html"
    }
    /* https的情况，暂未支持
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    */
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }
    // m_url为"/"时给定默认页面，index.html
    if (strlen(m_url) == 1) {
        strcat(m_url, "index.html");
    }
    // 解析完请求行，接下来解析请求头
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析请求头
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    // 遇到空行，表示头部字段解析完成
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 没有请求体则说明读到了最后一行空行，读取结束，返回GET_REQUEST
        return GET_REQUEST;
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // Host: 192.168.0.107:10000
        text += 5;
        // 使用strspn忽略掉Host:后面的空格，匹配上第一个不是空格的字符
        text += strspn(text, " \t");
        m_host = text;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_iflink = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atoi(text);
    } else {
        // 只获取了必需的头，其他的头没有解析
        // LOG_INFO("oop! Unknow header: %s\n", text);
    }
    return NO_REQUEST;
}

// 解析请求体，获得相关信息（目前是用户名和密码）
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if (m_read_idx >= m_content_length + m_check_idx) {
        text[m_content_length] = '\0';
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 从状态机，用于分析出一行内容，判断依据：\r\n
// 返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    // m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
    // m_check_idx指向从状态机当前正在分析的字节
    for (; m_check_idx < m_read_idx; ++m_check_idx) {
        // temp为将要分析的字节
        temp = read_buffer[m_check_idx];
        if (temp == '\r') { // 标志字符-存在读取到完整行的可能
            if (m_check_idx + 1 == m_read_idx) {
                // 下一个字符达到了buffer尾部，接收不完整，需要继续接收
                return LINE_OPEN;
            } else if (read_buffer[m_check_idx + 1] == '\n') { // 完整的一行
                read_buffer[m_check_idx++] = '\0';
                read_buffer[m_check_idx++] = '\0'; // 此时m_check_idx指向下一行
                return LINE_OK;
            }
            return LINE_BAD; // 都不符合，请求语法有误
        } else if (temp == '\n') {
            // temp为'\n'可能是因为上一次解析到了'\r'但读缓存不够，现在读到了随后的'\n'
            if (m_check_idx > 0 && read_buffer[m_check_idx - 1] == '\r') {
                read_buffer[m_check_idx - 1] = '\0';
                read_buffer[m_check_idx++] = '\0'; // 此时m_check_idx指向下一行
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN; // 没有找到\r\n，需要继续接收
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request() {
    // "/home/zht411/Anaconda/C++/webServerSelf/webserver"
    // 把网站的根目录拷贝到m_real_file，接下来会在这个根目录下找寻文件
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // 查看申请访问的地址，如果是post则根据/后端cgi标识来选择对应的资源
    const char* tmp = strrchr(m_url, '/');
    // m_url和tmp应该是相等
    if (cgi == 1 && (*(tmp + 1) == '2' || *(tmp + 1) == '3')) {
        // 2 登陆 3 注册
        char name[100], password[100];
        // 提取用户名和密码
        // 将用户名和密码提取出来
        // user=123&password=123
        int n = strlen(m_string);
        int idx = 0, i;
        for (i = 5; i < n && m_string[i] != '&'; ++i) {
            name[idx++] = m_string[i];
        }
        name[idx] = '\0';
        idx = 0, i += 10;
        for (; i < n && m_string[i] != '\0'; ++i) {
            password[idx++] = m_string[i];
        }
        password[idx] = '\0';
        string test_name(name);
        string test_password(MD5(password).toString());
        // 获取成功，接下来注册或登录
        if (*(tmp + 1) == '3') {
            // 如果是注册，先检测合法性
            if (user_info.find(test_name) != user_info.end()) {
                strcpy(m_url, "/registerError.html");
            } else {
                string sql("INSERT INTO user_info (name, password) VALUES ('" +
                           test_name + "', '" + test_password + "');");
                ConnectionPool* tmp = ConnectionPool::get_pool();
                std::shared_ptr<Connection> p = tmp->get_connection();
                MYSQL_RES* result = nullptr;
                // 更新数据库
                m_lock.lock();
                bool ret = p->update(sql);
                if (ret) {
                    user_info[test_name] = test_password;
                    strcpy(m_url, "/log.html");
                } else {
                    strcpy(m_url, "/registerError.html");
                }
                m_lock.unlock();
                LOG_INFO("%s",
                         string("新用户" + test_name + "注册成功").c_str());
                Log::get_instance()->flush();
            }
        } else {
            // 登陆
            if (user_info.find(test_name) == user_info.end()) {
                // 没有这个用户
                strcpy(m_url, "/logError.html");
            } else {
                if (user_info[test_name] == test_password) {
                    strcpy(m_url, "/welcome.html");
                    LOG_INFO("%s", string("用户" + test_name + "登陆").c_str());
                    Log::get_instance()->flush();
                } else {
                    strcpy(m_url, "/logError.html");
                }
            }
        }
    }
    char ch = *(tmp + 1);
    auto process_url = [this, len](const char* real) {
        char* m_url_real = (char*)malloc(sizeof(char) * 200);
        strcpy(m_url_real, real);
        // 对网站目录和real实际地址进行拼接
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    };
    switch (ch) {
        case '0': { // 请求资源为/0，跳转注册页面
            process_url("/register.html");
            break;
        }
        case '1': { // 请求资源为/1，跳转登陆界面
            process_url("/log.html");
            break;
        }
        case '5': { // 请求资源为/1，跳转登陆界面
            process_url("/picture.html");
            break;
        }
        case '6': { // 请求资源为/1，跳转登陆界面
            process_url("/video.html");
            break;
        }
        case '7': { // 请求资源为/1，跳转登陆界面
            process_url("/fans.html");
            break;
        }
        default: { // 在根目录后追加请求资源
            strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
        }
    }
    // 检查是否有所需要的资源文件，返回值为-1表示写入属性失败，也即没有资源
    if (stat(m_real_file, &m_file_stat) < 0) {
        LOG_INFO("没有资源：%s\n", m_real_file);
        return NO_RESOURCE;
    }

    // 判断访问权限(可读)
    if (!(m_file_stat.st_mode & S_IROTH)) {
        LOG_INFO("请求访问文件不可读，拒绝请求\n");
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        // 请求的是目录，错误的请求
        if (strncmp(m_real_file, doc_root, strlen(doc_root)) == 0) {
            // 如果访问的是网站的根目录，返回默认网页
            strcpy(m_real_file + strlen(m_real_file), "index.html");
        } else
            return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    // 创建内存映射
    // 设置为0时表示由系统决定映射区的起始地址
    // PROT_READ页内容能够被读取
    // MAP_PRIVATE表示内存区域的写入不会影响原文件，是一个私有映射
    m_file_address =
        (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // 关闭文件描述符，防止被文件描述被过度占用
    close(fd);
    return FILE_REQUEST;
}

// 解除映射
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}

http_conn::FILETYPE http_conn::refresh_content_type() {
    int url_len = strlen(m_url);
    auto type = HTML;
    if (url_len > 2 && strcmp(m_url + url_len - 2, "js") == 0) {
        type = JS;
    } else if (url_len > 3 && strcmp(m_url + url_len - 3, "css") == 0) {
        type = CSS;
    } else if (url_len > 4 && strcmp(m_url + url_len - 4, "html") == 0) {
        type = HTML;
    }
    return type;
}

bool http_conn::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    // 定义可变参数列表
    va_list arg_list;
    // 将arg_list初始化为传入参数
    va_start(arg_list, format);
    // 将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(write_buffer + m_write_idx,
                        WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) && add_content_type() &&
           add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

bool http_conn::add_content_type() {
    auto type = refresh_content_type();
    switch (type) {
        case HTML:
            return add_response("Content-Type:%s\r\n", "text/html");
        case JS:
            return add_response("Content-Type:%s\r\n", "application/json");
        case CSS:
            return add_response("Content-Type:%s\r\n", "text/css");
    }
}

bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n",
                        (m_iflink == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() { return add_response("%s", "\r\n"); }

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        // 内部错误，500
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        // 报文语法有误，404
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        // 资源不存在，404
        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        // 资源没有访问权限，403
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        //文件存在，200
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = write_buffer;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;

            return true;

        default:
            return false;
    }
    // 除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = write_buffer;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::init_mysql_result(ConnectionPool* conn_pool) {
    std::shared_ptr<Connection> p = conn_pool->get_connection();
    string sql = "select name, password from user_info;";
    // 在user表中检索username，passwd数据，浏览器端输入
    MYSQL_RES* result = nullptr;
    // 从表中检索完整的结果集
    result = p->query(sql);
    if (!result) {
        LOG_ERROR("数据库查询失败\n");
        return;
    }
    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    // 返回所有字段结构的数组
    MYSQL_FIELD* fields = mysql_fetch_field(result);
    // 从结果集里获取下一行，将对应的用户名和密码存到map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        string name(row[0]);
        string password(row[1]);
        user_info[name] = password;
    }
}