#include <iostream>
#include <pthread.h>
#include <time.h>

int main() {
    pthread_mutex_t lock;
    pthread_mutex_lock(&lock);
    time_t start = time(NULL);
    while (1) {
        if (time(NULL) - start > 5) break;
    }
    pthread_mutex_lock(&lock);
    return 0;
}