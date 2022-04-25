#include "timer.h"

// 升序链表初始化，头尾节点置为空指针
sort_timer_list::sort_timer_list() : head(nullptr), tail(nullptr) {}

// 升序链表析构，遍历整个链表，delete所有节点
sort_timer_list::~sort_timer_list() {
    while (head) {
        m_timer* temp = head;
        head = head->next;
        delete temp;
    }
}

// 添加定时器
void sort_timer_list::add_timer(m_timer* timer) {
    // 添加的定时器不能是空指针
    if (!timer)
        return;
    // 如果头节点是空的，就说明链表是空的，则添加的定时器是链表内唯一的定时器
    if (!head) {
        head = tail = timer;
        return;
    }
    // 比较要添加的定时器到时时间与头节点的到时时间，如果比头节点的小，那么它就是头节点
    if (timer->expire < head->expire) {
        timer->pre = nullptr;
        timer->next = head;
        head->pre = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}

// 修改定时器
void sort_timer_list::mod_timer(m_timer* timer) {
    // 定时器是空指针
    if (!timer)
        return;
    // 定时器调整后的超时时间仍然小于它的下一个定时器
    if (!timer->next || (timer->next->expire > timer->expire))
        return;

    // 如果修改的定时器是头定时器
    if (timer == head) {
        head = head->next;
        head->pre = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    } else {
        // 修改的定时器在内部，正常修改
        m_timer* prev = timer->pre;
        m_timer* next = timer->next;
        prev->next = next;
        next->pre = prev;
        add_timer(timer, next);
    }
}

// 删除定时器
void sort_timer_list::del_timer(m_timer* timer) {
    // 空节点是不能删的
    if (!timer) {
        // 异常情况
        return;
    } else if (timer == head && timer == tail) {
        // 只有一个定时器
        head = nullptr;
        tail = nullptr;
        delete timer;
        return;
    } else if (timer == head) {
        // 被删除的定时器在头部
        head = head->next;
        head->pre = nullptr;
        delete timer;
        return;
    } else if (timer == tail) {
        // 被删除的定时器在尾部
        tail = tail->pre;
        tail->next = nullptr;
        delete timer;
        return;
    } else {
        // 被删除的定时器在链表内部，常规链表结点删除
        m_timer* prev = timer->pre;
        m_timer* next = timer->next;
        prev->next = next;
        next->pre = prev;
        delete timer;
    }
}

// 私有成员，被公有成员add_timer和mod_time调用
// 主要用于调整链表内部结点
void sort_timer_list::add_timer(m_timer* timer, m_timer* start) {
    m_timer* prev = start;
    m_timer* temp = prev->next;
    // 遍历整个链表，找到第一个比要插入的节点的到时时间大的节点
    while (temp) {
        if (temp->expire > timer->expire) {
            prev->next = timer;
            timer->next = temp;
            temp->pre = timer;
            timer->pre = prev;
            break;
        }
        prev = temp;
        temp = temp->next;
    }
    // 说明遍历到了最后，要插入的节点就是最大的节点，将其放到队尾
    if (!temp) {
        prev->next = timer;
        timer->pre = prev;
        timer->next = nullptr;
        tail = timer;
    }
}

// 定时任务处理函数
void sort_timer_list::tick() {
    // 链表为空，无需计时
    if (!head) {
        return;
    }
    printf("%s\n", "timer tick");
    // 获取当前时间
    time_t curr = time(nullptr);
    m_timer* temp = head;
    while (temp) {
        // 链表容器为升序排列
        // 当前时间小于定时器的超时时间，后面的定时器也没有到期
        if (curr < temp->expire) {
            break;
        }
        temp->cb_func(temp->user_data);
        head = temp->next;
        if (head) {
            head->pre = nullptr;
        }
        delete temp;
        temp = head;
    }
}