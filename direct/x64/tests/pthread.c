#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>
#include <signal.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int ready=0,go=0,total=0;
static _Thread_local int value;
static void* signal_worker(void* arg) {
    int* received=arg;sigset_t set;sigemptyset(&set);sigaddset(&set,SIGUSR1);
    siginfo_t info;struct timespec timeout={2,0};
    int result=sigtimedwait(&set,&info,&timeout);
    *received=result==SIGUSR1 && info.si_signo==SIGUSR1 && info.si_pid==getpid();
    return 0;
}
static void* worker(void* arg) {
    int id=(int)(intptr_t)arg;
    value=id;errno=100+id;
    pthread_mutex_lock(&lock);++ready;pthread_cond_broadcast(&condition);
    while(!go) pthread_cond_wait(&condition,&lock);
    pthread_mutex_unlock(&lock);
    for(int i=0;i<1000;i++) {
        if(value!=id || errno!=100+id || syscall(SYS_gettid)<=0) return (void*)1;
        pthread_mutex_lock(&lock);++total;pthread_mutex_unlock(&lock);
    }
    return 0;
}
int main(void) {
    pthread_t threads[4];
    for(int i=0;i<4;i++) {
        int error=pthread_create(&threads[i],0,worker,(void*)(intptr_t)(i+1));
        if(error) { fprintf(stderr,"pthread_create: %d\n",error);return 10; }
    }
    pthread_mutex_lock(&lock);
    while(ready!=4) pthread_cond_wait(&condition,&lock);
    go=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&lock);
    for(int i=0;i<4;i++) {void* result=0;if(pthread_join(threads[i],&result) || result) return 11;}
    if(total!=4000) return 12;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_nsec+=10000000;
    if(deadline.tv_nsec>=1000000000) {deadline.tv_nsec-=1000000000;++deadline.tv_sec;}
    pthread_mutex_lock(&lock);int result=pthread_cond_timedwait(&condition,&lock,&deadline);pthread_mutex_unlock(&lock);
    if(result!=ETIMEDOUT) return 13;
    sigset_t set;sigemptyset(&set);sigaddset(&set,SIGUSR1);
    if(pthread_sigmask(SIG_BLOCK,&set,0)) return 14;
    int received=0;pthread_t signal_thread;
    if(pthread_create(&signal_thread,0,signal_worker,&received)) return 15;
    if(pthread_kill(signal_thread,SIGUSR1) || pthread_join(signal_thread,0) || !received) return 16;
    struct timespec zero={0,0};errno=0;
    if(sigtimedwait(&set,0,&zero)!=-1 || errno!=EAGAIN) return 17;
    puts("NATIVE_X64_PTHREAD_PASS (4 workers, independent TLS, mutex, condition, join, timed wait, queued signal)");
    return 0;
}
