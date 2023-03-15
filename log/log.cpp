#include "log.h"
#include "sys/time.h"

//异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char* file_name,
               int buf_size,
               int split_lines,
               int max_queue_size) {
    // 如果设置了队列长度，则异步处理
    if (max_queue_size >= 1) {
        m_is_async = true;
        m_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        // flush_log_thread为回调函数，这里表示创建线程异步写日志
        pthread_create(&tid, nullptr, flush_log_thread, nullptr);
    }

    // 输出内容的长度
    m_log_buf_size = buf_size;
    // 日志的最大行数
    m_log_split_lines = split_lines;
    // 正常过程应该只初始化一次，这里防止特殊情况
    if (m_buf != nullptr) {
        delete[] m_buf;
    }
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', sizeof(m_log_buf_size));

    time_t t = time(nullptr);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 从后往前找到第一个/的位置
    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    // 相当于自定义log到文件名，如果没输入文件名就用时间+文件名作为日志名
    if (p == nullptr) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900,
                 my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else {
        // 将/的位置向后移动一个位置，然后复制到logname中
        // p - file_name + 1是文件所在路径文件夹的长度
        // dirname相当于./
        strcpy(m_log_name, p + 1);
        strncpy(m_log_dir, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", m_log_dir,
                 my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                 m_log_name);
    }

    m_today = my_tm.tm_mday;
    // "a"追加到一个文件。
    // 写操作向文件末尾追加数据。如果文件不存在，则创建文件。
    m_file = fopen(log_full_name, "a");
    if (m_file == nullptr) {
        return false;
    }
    return true;
}

// 写日志
void Log::write_log(int level, const char* format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t = now.tv_sec;
    // t的值被分解为tm结构
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    switch (level) {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[error]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }
    // 写入一个log，对m_count++，m_split_lines最大行数
    m_lock.lock();
    m_count++;

    // 判断日期，如果不是当天需要新开一个日志文件
    if (m_today != my_tm.tm_mday || m_count % m_log_split_lines == 0) {
        char new_log[256] = {0};
        fflush(m_file);
        fclose(m_file);
        char tail[16] = {0};

        // 格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900,
                 my_tm.tm_mon + 1, my_tm.tm_mday);
        // 如果是时间不是今天,则创建今天的日志，更新m_today和m_count
        if (m_today != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", m_log_dir, tail, m_log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        } else {
            // 超过了最大行，在之前的日志名基础上加后缀, m_count/m_split_lines
            snprintf(new_log, 255, "%s%s%s_%lld", m_log_dir, tail, m_log_name,
                     m_count / m_log_split_lines);
        }
        m_file = fopen(new_log, "a");
    }
    m_lock.unlock();
    //将传入的format参数赋值给valst，便于格式化输出
    va_list valist;
    va_start(valist, format);

    string log_str;
    m_lock.lock();
    //写入内容格式：时间 + 内容
    //时间格式化，snprintf成功返回写字符的总数，其中不包括结尾的null字符
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 内容格式化，用于向字符串中打印数据、数据格式用户自定义
    // 返回写入到字符数组str中的字符个数(不包含终止符)
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valist);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;
    m_lock.unlock();

    if (m_is_async && !m_queue->full()) {
        m_queue->push(log_str);
    } else {
        m_lock.lock();
        fputs(log_str.c_str(), m_file);
        m_lock.unlock();
    }

    va_end(valist);
}

void Log::flush(void) {
    m_lock.lock();
    //强制刷新写入流缓冲区
    fflush(m_file);
    m_lock.unlock();
}