#include <stdio.h>
#include <thread>

#include "hobjectpool.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

void msleep(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms*1000);
#endif
}

class Task {
public:
    Task() {printf("Task()\n");}
    ~Task() {printf("~Task()\n");}

    void Do() {
        printf("%p start do...\n", this);
        msleep(4000);
        printf("%p end do\n", this);
    }
};

HObjectPool<Task> tasks;

void task_thread(int id) {
    printf("thread %d run...\n", id);
    auto pObj = tasks.Get();
    if (pObj) {
        pObj->Do();
        tasks.Release(pObj);
    }
    else {
        printf("objects too little\n");
    }
    printf("thread %d exit\n", id);
}

int main(int argc, char** argv) {
    tasks.pool_size = 5;
    tasks.timeout = 3000;
    for (int i = 0; i < 10; ++i) {
        new std::thread(task_thread, i);
    }
    msleep(5000);
    for (int i = 10; i < 20; ++i) {
        new std::thread(task_thread, i);
    }
    msleep(10000);
    return 0;
}
