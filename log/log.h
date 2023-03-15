#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include "block_queue.h"

using std::string;

class Log {
   public:
    // 单例模式，C++11以后懒汉模式不用加锁
    static Log* get_instance() {
        static Log log;
        return &log;
    }

    // 异步写日志公有方法，调用私有方法async_write_log
    static void* flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
        return nullptr;
    }

    // 初始化
    // 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char* file_name,
              int buf_size,
              int split_lines,
              int max_queue_size);
    void write_log(int level, const char* format, ...);
    void flush(void);

   private:
    Log() {
        m_count = 0;
        m_is_async = false;
    }
    ~Log() {
        if (m_file != nullptr) {
            fclose(m_file);
        }
    }
    void async_write_log() {
        string single_log;
        // 从阻塞队列中取出一个日志string，写入到文件里
        while (m_queue->pop(single_log)) {
            m_lock.lock();
            fputs(single_log.c_str(), m_file);
            m_lock.unlock();
        }
    }

   private:
    char m_log_dir[128];    //路径名
    char m_log_name[128];   // log文件名
    int m_log_split_lines;  // log最大行数
    int m_log_buf_size;     // log缓冲区大小
    int m_today;            // 记录日期，按天分类

    FILE* m_file;                  //  打开log文件的文件指针
    block_queue<string>* m_queue;  // 阻塞队列用来存log
    bool m_is_async;               // 是否同步标志位
    locker m_lock;                 // log的操作锁
    char* m_buf;
    long long m_count;  // 日志行数
};

#define LOG_DEBUG(format, ...) \
    Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) \
    Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) \
    Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) \
    Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif