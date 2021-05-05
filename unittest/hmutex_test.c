#include "hthread.h"
#include "hmutex.h"
#include "htime.h"

void once_print() {
    printf("exec once\n");
}

HTHREAD_ROUTINE(test_once) {
    honce_t once = HONCE_INIT;
    for (int i = 0; i < 10; ++i) {
        honce(&once, once_print);
    }
    printf("honce test OK!\n");
    return 0;
}

HTHREAD_ROUTINE(test_mutex) {
    hmutex_t mutex;
    hmutex_init(&mutex);
    hmutex_lock(&mutex);
    hmutex_unlock(&mutex);
    hmutex_destroy(&mutex);
    printf("hmutex test OK!\n");
    return 0;
}

HTHREAD_ROUTINE(test_recursive_mutex) {
    hrecursive_mutex_t mutex;
    hrecursive_mutex_init(&mutex);
    hrecursive_mutex_lock(&mutex);
    hrecursive_mutex_lock(&mutex);
    hrecursive_mutex_unlock(&mutex);
    hrecursive_mutex_unlock(&mutex);
    hrecursive_mutex_destroy(&mutex);
    printf("hrecursive_mutex test OK!\n");
    return 0;
}

HTHREAD_ROUTINE(test_spinlock) {
    hspinlock_t spin;
    hspinlock_init(&spin);
    hspinlock_lock(&spin);
    hspinlock_unlock(&spin);
    hspinlock_destroy(&spin);
    printf("hspinlock test OK!\n");
    return 0;
}

HTHREAD_ROUTINE(test_rwlock) {
    hrwlock_t rwlock;
    hrwlock_init(&rwlock);
    hrwlock_rdlock(&rwlock);
    hrwlock_rdunlock(&rwlock);
    hrwlock_wrlock(&rwlock);
    hrwlock_wrunlock(&rwlock);
    hrwlock_destroy(&rwlock);
    printf("hrwlock test OK!\n");
    return 0;
}

HTHREAD_ROUTINE(test_timed_mutex) {
    htimed_mutex_t mutex;
    htimed_mutex_init(&mutex);
    htimed_mutex_lock(&mutex);
    time_t start_time = gettick_ms();
    htimed_mutex_lock_for(&mutex, 3000);
    time_t end_time = gettick_ms();
    printf("htimed_mutex_lock_for %zdms\n", end_time - start_time);
    htimed_mutex_unlock(&mutex);
    htimed_mutex_destroy(&mutex);
    printf("htimed_mutex test OK!\n");
    return 0;
}

HTHREAD_ROUTINE(test_condvar) {
    hmutex_t mutex;
    hmutex_init(&mutex);
    hcondvar_t cv;
    hcondvar_init(&cv);

    hmutex_lock(&mutex);
    hcondvar_signal(&cv);
    hcondvar_broadcast(&cv);
    time_t start_time = gettick_ms();
    hcondvar_wait_for(&cv, &mutex, 3000);
    time_t end_time = gettick_ms();
    printf("hcondvar_wait_for %zdms\n", end_time - start_time);
    hmutex_unlock(&mutex);

    hmutex_destroy(&mutex);
    hcondvar_destroy(&cv);
    printf("hcondvar test OK!\n");
    return 0;
}

HTHREAD_ROUTINE(test_sem) {
    hsem_t sem;
    hsem_init(&sem, 10);
    for (int i = 0; i < 10; ++i) {
        hsem_wait(&sem);
    }
    hsem_post(&sem);
    hsem_wait(&sem);
    time_t start_time = gettick_ms();
    hsem_wait_for(&sem, 3000);
    time_t end_time = gettick_ms();
    printf("hsem_wait_for %zdms\n", end_time - start_time);
    hsem_destroy(&sem);
    printf("hsem test OK!\n");
    return 0;
}

int main(int argc, char* argv[]) {
    hthread_t thread_once = hthread_create(test_once, NULL);
    hthread_t thread_mutex = hthread_create(test_mutex, NULL);
    hthread_t thread_recursive_mutex = hthread_create(test_recursive_mutex, NULL);
    hthread_t thread_spinlock = hthread_create(test_spinlock, NULL);
    hthread_t thread_rwlock = hthread_create(test_rwlock, NULL);
    hthread_t thread_timed_mutex = hthread_create(test_timed_mutex, NULL);
    hthread_t thread_condvar = hthread_create(test_condvar, NULL);
    hthread_t thread_sem = hthread_create(test_sem, NULL);

    hthread_join(thread_once);
    hthread_join(thread_mutex);
    hthread_join(thread_recursive_mutex);
    hthread_join(thread_spinlock);
    hthread_join(thread_rwlock);
    hthread_join(thread_timed_mutex);
    hthread_join(thread_condvar);
    hthread_join(thread_sem);
    printf("hthread test OK!\n");
    return 0;
}
