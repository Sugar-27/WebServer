#include "connectionPool.h"

// 懒汉单例模式，只有调用get函数时才生成对象
ConnectionPool* ConnectionPool::get_pool() {
    static ConnectionPool m_pool;
    return &m_pool;
}

// 预处理配置文件
bool ConnectionPool::deal_config() {
    FILE* m_config = fopen("mysql.conf", "r");
    if (!m_config) {
        LOG("没有配置文件，请先配置好文件再次运行");
        return false;
    }
    while (!feof(m_config)) {
        char buf[1024] = {0};
        size_t len = sizeof buf;
        fgets(buf, sizeof buf, m_config);
        string _buf(buf);
        int idx = _buf.find('=');
        if (idx == -1)
            continue;
        string key = _buf.substr(0, idx);
        string val = _buf.substr(idx + 1, _buf.find('\n', idx) - idx - 1);
        // if (val.back() == '\n') val.pop_back();
        // cout << "key:" << key << endl;
        // cout << "value:" << val << endl;
        if (key == "ip")
            m_ip = val;
        else if (key == "port")
            m_port = atoi(val.c_str());
        else if (key == "username")
            m_user = val;
        else if (key == "password")
            m_password = val;
        else if (key == "dbname")
            m_dbname = val;
        else if (key == "initSize")
            m_init_size = atoi(val.c_str());
        else if (key == "maxSize")
            m_max_size = atoi(val.c_str());
        else if (key == "maxIdleTime")
            m_max_idletime = atoi(val.c_str());
        else if (key == "connectionTimeout")
            m_timeout = atoi(val.c_str());
    }
    return true;
}

// 数据库连接池的构造函数
ConnectionPool::ConnectionPool() : m_port(3306) {
    // 预处理，用配置文件对数据库连接池要连接的数据库属性进行配置，就不需要重新编译代码了
    if (!deal_config()) {
        LOG("未能完成连接池的初始化");
        return;
    }
    for (size_t i = 0; i < m_init_size; ++i) {
        Connection* p = new Connection();
        p->connect(m_ip, m_user, m_password, m_dbname);
        p->refresh_alive_time();
        m_connection_queue.push(p);
        m_connection_cnt++;
    }
    // 启动一个生产者线程
    pthread_t produce;
    pthread_create(&produce, nullptr, ConnectionPool::produce_connection, this);
    // 设置线程分离，使得线程的结束由系统接管
    if (pthread_detach(produce) != 0) {
        LOG("生产者线程未能成功产生");
        throw std::exception();
    }
    // 启动一个定时线程，起到定时器的作用，时间一到对队列进行扫描，如果有超时的就扔出去
    // thread scanner_connection_time(
    //     std::bind(&ConnectionPool::scan_connection_time, this));
    pthread_t t;
    pthread_create(&t, nullptr, ConnectionPool::scan_connection_time, this);
    if (pthread_detach(t) != 0) {
        LOG("定时器线程未能成功产生");
        throw std::exception();
    }
}

void ConnectionPool::print() {
    cout << m_ip;
    cout << m_user;
    cout << m_password;
    cout << m_dbname;
    cout << m_port;
    cout << m_init_size;
    cout << m_max_size;
    cout << m_max_idletime;
    cout << m_timeout << endl;
}

// 生产者用于生产连接的函数
void* ConnectionPool::produce_connection(void* arg) {
    ConnectionPool* pool = (ConnectionPool*)arg;
    while (true) {
        // 利用unique_lock在一个while循环内实现自动加锁解锁
        pool->m_queue_mutex.lock();
        while (!pool->m_connection_queue.empty()) {
            // 队列内还有空闲连接
            /* 先unlock之前获得的mutex，然后阻塞当前的执行线程。把当前线程添加到等待线程列表中，该线程会持续
             * block 直到被 notify_all() 或 notify_one()
             * 唤醒。被唤醒后，该thread会重新获取mutex，获取到mutex后执行后面的动作*/
            pool->cv.wait(pool->m_queue_mutex.get());
        }
        // 总的连接数不能超过上限
        if (pool->m_connection_cnt >= pool->m_max_size)
            continue;
        Connection* p = new Connection();
        p->connect(pool->m_ip, pool->m_user, pool->m_password, pool->m_dbname,
                   pool->m_port);
        // p->connect("localhost", "root", "root", "chat");
        p->refresh_alive_time();
        pool->m_connection_queue.push(p);
        ++pool->m_connection_cnt;
        // 生产完毕，通知可以使用
        pool->cv.broadcast();
        pool->m_queue_mutex.unlock();
    }
}

// 定时器线程的处理函数
void* ConnectionPool::scan_connection_time(void* arg) {
    ConnectionPool* pool = (ConnectionPool*) arg;
    // 扫描队列里全部的节点，将超过最大允许存活时间的去掉
    while (true) {
        sleep(pool->m_max_idletime);
        pool->m_queue_mutex.lock();
        while (pool->m_connection_cnt) {
            Connection* p = pool->m_connection_queue.front();
            // cout << p->get_alive_time() << endl;
            if (p->get_alive_time() > pool->m_max_idletime * 100 &&
                pool->m_connection_cnt > pool->m_init_size / 2 + 1) {
                pool->m_connection_queue.pop();
                --pool->m_connection_cnt;
                delete p;
            } else {
                // 队头的连接没有超时则后面的肯定不会超时
                break;
            }
        }
        pool->m_queue_mutex.unlock();
    }
}

// 消费者线程用来获取连接的函数
std::shared_ptr<Connection> ConnectionPool::get_connection() {
    m_queue_mutex.lock();
    while (m_connection_queue.empty()) {
        // 等待队列为空
        // cv.notify_all();  // 让生产队列生产一个连接出来
        // if (std::cv_status::timeout ==
        //     cv.wait(lock, std::chrono::milliseconds(m_timeout))) {
        //     // 如果不是被唤醒，而是因为超时了
        //     if (m_connection_queue.empty()) {
        //         LOG("申请连接失败");
        //         return nullptr;
        //     }
        // }
        cv.wait(m_queue_mutex.get());
    }
    // 使用智能指针，并重写删除器，将原始指针归还到队列里面而不是析构
    std::shared_ptr<Connection> p(m_connection_queue.front(),
                                  [this](Connection* tmp) {
                                      // std::unique_lock<std::mutex>
                                      // lock(m_queue_mutex);
                                      m_queue_mutex.lock();
                                      tmp->refresh_alive_time();
                                      m_connection_queue.push(tmp);
                                      m_queue_mutex.unlock();
                                  });
    m_connection_queue.pop();
    cv.broadcast();  // 消费者取出一个，通知生产者生产
    m_queue_mutex.unlock();
    return p;
}