#include "block_queue.h"
#include "log.h"
#include <iostream>

using namespace std;

int main() {
    Log::get_instance()->init("ServerLog", 2000, 800000, 0);  //同步日志模型
    LOG_ERROR("%s", "epoll failure");
    // block_queue<string> a(6);
    // a.push("abcd");
    // string test;
    // a.front(test);
    // cout << test << endl;

    return 0;
}