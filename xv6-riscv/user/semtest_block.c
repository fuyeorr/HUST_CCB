#include "kernel/types.h"
#include "user/user.h"

int
main()
{
    int sem_id;

    // 创建一个二元信号量（互斥锁），初始值 1
    sem_id = sem_open(1, "mutex");
    if (sem_id < 0) {
        printf("sem_open failed\n");
        exit(1);
    }

    if (fork() == 0) {
        // 子进程
        sem_wait(sem_id);
        printf("child: critical section start\n");
        printf("child: critical section end\n");
        sem_signal(sem_id);
        exit(0);
    } else {
        // 父进程
        sem_wait(sem_id);
        printf("parent: critical section start\n");
        printf("parent: critical section end\n");
        sem_signal(sem_id);
        wait(0);
        printf("\n=== Gantt Chart ===\n");
        sem_gantt(sem_id);
        sem_close(sem_id);
    }
    exit(0);
}
