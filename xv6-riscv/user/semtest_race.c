#include "kernel/types.h"
#include "user/user.h"

#define N_PROCS 20

void atomic_printf(int sem_id, char* msg, int id) {
  sem_wait(sem_id);
  printf(msg, id);
  sem_signal(sem_id);
}

int
main()
{
    int sem_id;
    int sem_printf_id;

    sem_id = sem_open(5, "resources");
    sem_printf_id = sem_open(1, "printf_mutex");
    if (sem_id < 0 || sem_printf_id < 0) {
        printf("sem_open failed\n");
        exit(1);
    }

    for(int i = 1; i <= N_PROCS; i++) {
      int pid = fork();
      if (pid == 0) {
        atomic_printf(sem_printf_id, "Child %d waiting...\n", i);
        sem_wait(sem_id);

        atomic_printf(sem_printf_id, "Child %d processing...\n", i);
        pause(10); // userspace sleep
        
        atomic_printf(sem_printf_id, "Child %d leaving...\n", i);
        sem_signal(sem_id);
        
        exit(0);
      }
    }

    for(int i = 1; i <= N_PROCS; i++) {
      wait(0);
    }

    printf("\n=== Gantt Chart for \"resources\" ===\n");
    sem_gantt(sem_id);
    printf("\n=== Gantt Chart for \"printf_mutex\" ===\n");
    sem_gantt(sem_printf_id);

    sem_close(sem_id);
    sem_close(sem_printf_id);

    exit(0);
}
