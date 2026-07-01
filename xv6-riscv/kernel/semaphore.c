#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "semaphore.h"
#include "defs.h"

void
sem_init (struct semaphore *sem, int value)
{
    sem->count = value;
    initlock(&sem->lock, "semaphore");
};

void
sem_wait(struct semaphore *sem)
{
    acquire(&sem->lock);    //获取内部锁
    while (sem->count <=0)
        //sleep 原子操作 「当前进程进入等待队列」、「释放锁」、「让出CPU」
        sleep(sem, &sem->lock);
    sem->count--;   //获取资源
    release(&sem->lock);    //释放内部锁
}

void
sem_signal(struct semaphore *sem)
{
    acquire(&sem->lock);
    sem->count++;   //归还资源
    //我chovy,起床辣!
    wakeup(sem);
    release(&sem->lock);
}