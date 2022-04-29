#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <cstring>
#include <iostream>
#include <string>

using namespace std;

int mon_log(char* format, ...) {
    char str_tmp[50];
    int i = 0;
    va_list vArgList;  //定义一个va_list型的变量,这个变量是指向参数的指针.
    va_start(
        vArgList,
        format);  //用va_start宏初始化变量,这个宏的第二个参数是第一个可变参数的前一个参数,是一个固定的参数
    i = vsnprintf(str_tmp, 50, format, vArgList);
    printf("%s\n", str_tmp);
    va_end(vArgList);  //用va_end宏结束可变参数的获取
    return i;          //返回参数的字符个数中间有逗号间隔
}
//调用上面的函数
int main() {
    // int i = mon_log("%s,%d,%d,%d", "asd", 2, 3, 4);
    // printf("%d\n", i);
    char name[100], password[100];
    for (int i = 0; i < 5; ++i) name[i] = 'a' + i;
    name[5] = '\0';
    string test_name(name);
    string test_password("12345");
    string test("INSERT INTO user_infor (name, password, age, sex) VALUES ('Tom', 'test_password', 10, 'male');");
    string sql("INSERT INTO user_infor (name, password) VALUES ('" + test_name + "', '" + test_password + "');");
    cout << test << endl;
    cout << sql << endl;

    return 0;
}