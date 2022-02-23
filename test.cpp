#include <string.h>
#include <stdio.h>
#include <iostream>
#include <sys/stat.h>

using namespace std;

int main() {
    char* a = "/home/zht411/Anaconda/C++/webServerSelf/webserver/favicon.ico";
    struct stat m_file_stat;
    if (stat(a, &m_file_stat) < 0) {
        printf("没有资源：%s\n", a);}
    return 0;
}