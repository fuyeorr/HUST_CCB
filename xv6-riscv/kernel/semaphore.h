#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "types.h"
#include "spinlock.h"

#define SEM_EVENT_LOG_SIZE 1024

enum sem_event_type {
    SEM_EVENT_WAIT,
    SEM_EVENT_ACQUIRE,
    SEM_EVENT_RELEASE,
};

struct sem_event {
    int sem_id;
    int pid;
    uint ticks;
    uint8 type;
    char pname[16];
};

extern struct sem_event sem_event_log[SEM_EVENT_LOG_SIZE];
extern int sem_event_index;
extern struct spinlock sem_event_lock;

struct semaphore {
  int count;
  struct spinlock lock;
  int active;
  char name[16];
};

#define NSEM 128
extern struct semaphore sem_table[NSEM];

void sem_init(struct semaphore *sem, int value);
void sem_wait(struct semaphore *sem);
void sem_signal(struct semaphore *sem);

void sem_record_event(int sem_id, int pid, uint ticks, int type);
void sem_event_log_init(void);
void sem_gantt(int sem_id);

#endif

