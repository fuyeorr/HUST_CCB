#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "spinlock.h"

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
#endif