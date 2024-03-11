#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include "hloop.h"
#include "hevent.h"

#ifdef OS_LINUX
static int callback_triggered = 0;
static int exit_main = 0;

// 线程函数，用于修改全局变量
void *thread_function(void *arg) {
    printf("loop run.\n");
    hloop_run((hloop_t*)arg);

    return NULL;
}

void signal_handler(hsig_t* signal) {
    printf("Signal %d received.\n", signal->signal);
    // 在这里执行你的回调操作
    // 例如，设置标志以指示回调已触发
    callback_triggered += 1;
}

void timeout_handler(htimer_t* timer) {
    printf("timeout received.\n");
    exit_main = 1;
}

void test1()
{
    // 注册信号处理函数
    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
    hsig_t* signal = hsig_add(loop, signal_handler, SIGUSR1);
    hsig_add(loop, signal_handler, SIGUSR2);
    pthread_t thread_id;

    // 创建线程
    if (pthread_create(&thread_id, NULL, thread_function, loop) != 0) {
        perror("pthread_create");
        exit(1);
    }

    // 发送信号（这里使用 SIGUSR1 作为示例）
    pid_t pid = getpid();   // 获取当前进程的进程ID
    printf("Sending signal SIGUSR1 to process %d...\n", pid);

    kill(pid, SIGUSR1);
    // 等待一段时间以确保回调有足够的时间执行
    sleep(1);

    kill(pid, SIGUSR2);
    // 等待一段时间以确保回调有足够的时间执行
    sleep(1);
    // 检查回调是否已触发
    if (2 == callback_triggered) {
        printf("Callback was triggered successfully.\n");
    } else {
        printf("Callback did not trigger.\n");
    }
}

void test2()
{
    callback_triggered = 0;
    // 注册信号处理函数
    hloop_t* loop = hloop_new(HLOOP_FLAG_AUTO_FREE);
    hsig_t* signal = hsig_add(loop, signal_handler, SIGUSR1);
    hsig_add(loop, signal_handler, SIGUSR2);
    pthread_t thread_id;
    int status;

    // 创建线程
    if (pthread_create(&thread_id, NULL, thread_function, loop) != 0) {
        perror("pthread_create");
        exit(1);
    }

    // 创建线程
    pid_t pid = 0;
    pid = fork();
    if (pid < 0)
        return 1;
    else if (pid == 0) {
        printf("sub process continue.\n");
        usleep(10000); /* 0.01 seconds */
        kill(getppid(), SIGUSR1);
        usleep(10000); /* 0.01 seconds */
        kill(getppid(), SIGUSR1);
        usleep(10000); /* 0.01 seconds */
        kill(getppid(), SIGUSR2);
        printf("sub process exit.\n");
    }
    else {
        printf("Main process continue.\n");
        (void)htimer_add(loop, timeout_handler, 5000, INFINITE);
        while (!exit_main) {
            sleep(1);
        }
        // 检查回调是否已触发
        if (3 == callback_triggered) {
            printf("Callback was triggered successfully.\n");
        } else {
            printf("Callback did not trigger.\n");
        }
    }
}
#endif

int main() {
#ifdef OS_LINUX
    test1();
    test2();
#endif
    return 0;
}
