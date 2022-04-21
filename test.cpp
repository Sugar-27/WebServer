#include <pthread.h>
#include <time.h>
#include <iostream>
#include <limits.h>
#include <stdlib.h>
#include <cstring>
#include <string>

using namespace std;

int main() {
    // cout << "hello " << endl;
    char p[1024] = "./webserver";
    realpath(p, p);
    cout << p << endl;
    return 0;
}