#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "semaphore.h"
#include "defs.h"

//初始化单个信号量
void 
sem_init(struct semaphore *sem, int value)
{
    sem->count = value;
    initlock(&sem->lock, "semaphore");
    sem->active = 0;
    memset(sem->name, 0, 16);
}

void
sem_wait(struct semaphore *sem)
{
    acquire(&sem->lock);
    sem->count--;
    if (sem->count < 0)
      sleep(sem, &sem->lock);
    release(&sem->lock);
}

void
sem_signal(struct semaphore *sem)
{
    acquire(&sem->lock);
    sem->count++;
    //我chovy,起床辣!
    if (sem->count <= 0)
        wakeup_one(sem);

    release(&sem->lock);
}
