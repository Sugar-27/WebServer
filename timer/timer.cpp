#include "timer.h"

sort_timer_list::sort_timer_list() : head(NULL), tail(NULL) {}

sort_timer_list::~sort_timer_list() {
    while (head) {
        m_timer* temp = head;
        head = head->next;
        delete temp;
    }
}

// 添加定时器
void sort_timer_list::add_timer(m_timer* timer) {
    if (!timer)
        return;
    if (!head) {
        head = tail = timer;
        return;
    }
    if (timer->expire < head->expire) {
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
        head->pre = NULL;
        timer->next = NULL;
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
    if (!timer) {
        // 异常情况
        return;
    }
    if (timer == head && timer == tail) {
        // 只有一个定时器
        head = NULL;
        tail = NULL;
        delete timer;
        return;
    } else if (timer == head) {
        // 被删除的定时器在头部
        head = head->next;
        head->pre = NULL;
        delete timer;
        return;
    } else if (timer == tail) {
        // 被删除的定时器在尾部
        tail = tail->pre;
        tail->next = NULL;
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
    if (!temp) {
        prev->next = timer;
        timer->pre = prev;
        timer->next = NULL;
        tail = timer;
    }
}

// 定时任务处理函数
void sort_timer_list::tick() {
    if (!head) {
        return;
    }
    printf("%s\n", "timer tick");
    // 获取当前时间
    time_t curr = time(NULL);
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
            head->pre = NULL;
        }
        delete temp;
        temp = head;
    }
}