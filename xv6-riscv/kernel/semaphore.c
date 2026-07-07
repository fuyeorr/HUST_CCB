#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "semaphore.h"
#include "proc.h"
#include "defs.h"

struct sem_event sem_event_log[SEM_EVENT_LOG_SIZE];
int sem_event_index = 0;
struct spinlock sem_event_lock;

void
sem_event_log_init(void)
{
    initlock(&sem_event_lock, "semevent");
    sem_event_index = 0;
    memset(sem_event_log, 0, sizeof(sem_event_log));
}

void
sem_record_event(int sem_id, int pid, uint ticks, int type)
{
    acquire(&sem_event_lock);
    int idx = sem_event_index % SEM_EVENT_LOG_SIZE;
    sem_event_log[idx].sem_id = sem_id;
    sem_event_log[idx].pid = pid;
    sem_event_log[idx].ticks = ticks;
    sem_event_log[idx].type = (uint8)type;
    safestrcpy(sem_event_log[idx].pname, myproc()->name, 16);
    sem_event_index++;
    release(&sem_event_lock);
}

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
    if (sem->count < 0) {
        // 资源不足, 进程将进入等待
        sem_record_event(sem - sem_table, myproc()->pid, ticks, SEM_EVENT_WAIT);
        sleep(sem, &sem->lock);
        // 被唤醒, 进程获得信号量
        sem_record_event(sem - sem_table, myproc()->pid, ticks, SEM_EVENT_ACQUIRE);
    } else {
        // 资源充足, 直接获得信号量
        sem_record_event(sem - sem_table, myproc()->pid, ticks, SEM_EVENT_ACQUIRE);
    }
    release(&sem->lock);
}

void
sem_signal(struct semaphore *sem)
{
    acquire(&sem->lock);
    // 进程释放信号量
    sem_record_event(sem - sem_table, myproc()->pid, ticks, SEM_EVENT_RELEASE);
    sem->count++;
    //我chovy,起床辣!
    if (sem->count <= 0)
        wakeup_one(sem);

    release(&sem->lock);
}

void
sem_gantt(int sem_id)
{
    if (sem_id < 0 || sem_id >= NSEM || sem_table[sem_id].active == 0) {
        printk("sem_gantt: invalid semaphore id %d\n", sem_id);
        return;
    }

    struct semaphore *sem = &sem_table[sem_id];

    #define MAX_GANTT_EVENTS 256
    #define MAX_GANTT_PIDS 32

    static struct {
        int pid;
        uint ticks;
        uint8 type;
        char pname[16];
    } events[MAX_GANTT_EVENTS];

    static int pids[MAX_GANTT_PIDS];
    static char pid_names[MAX_GANTT_PIDS][16];
    int pid_count = 0;

    int event_count = 0;
    uint t_min = 0xFFFFFFFF;
    uint t_max = 0;

    acquire(&sem_event_lock);
    int total = sem_event_index;
    int start = 0;
    if (total > SEM_EVENT_LOG_SIZE) {
        start = total - SEM_EVENT_LOG_SIZE;
        total = SEM_EVENT_LOG_SIZE;
    } else {
        total = sem_event_index;
    }

    for (int i = 0; i < total && event_count < MAX_GANTT_EVENTS; i++) {
        int idx = (start + i) % SEM_EVENT_LOG_SIZE;
        if (sem_event_log[idx].sem_id == sem_id) {
            events[event_count].pid = sem_event_log[idx].pid;
            events[event_count].ticks = sem_event_log[idx].ticks;
            events[event_count].type = sem_event_log[idx].type;
            safestrcpy(events[event_count].pname, sem_event_log[idx].pname, 16);
            event_count++;

            if (sem_event_log[idx].ticks < t_min)
                t_min = sem_event_log[idx].ticks;
            if (sem_event_log[idx].ticks > t_max)
                t_max = sem_event_log[idx].ticks;

            int found = 0;
            for (int j = 0; j < pid_count; j++) {
                if (pids[j] == sem_event_log[idx].pid) {
                    found = 1;
                    break;
                }
            }
            if (!found && pid_count < MAX_GANTT_PIDS) {
                pids[pid_count] = sem_event_log[idx].pid;
                safestrcpy(pid_names[pid_count], sem_event_log[idx].pname, 16);
                pid_count++;
            }
        }
    }
    release(&sem_event_lock);

    if (event_count == 0) {
        printk("sem_gantt: no events for semaphore \"%s\" (id=%d)\n",
               sem->name, sem_id);
        return;
    }

    printk("\n");
    printk("=== Semaphore Gantt Chart ===\n");
    printk("Sem: \"%s\" (id=%d)  value=%d  events=%d\n",
           sem->name, sem_id, sem->count, event_count);
    printk("Ticks: %d .. %d  (%d ticks total)\n",
           t_min, t_max, t_max - t_min + 1);
    printk("Legend: # = holding CS   W = waiting   . = idle\n");
    printk("\n");

    uint t_range = t_max - t_min + 1;
    int char_width;
    int tick_per_char;

    if (t_range <= 80) {
        char_width = t_range;
        tick_per_char = 1;
    } else if (t_range <= 160) {
        char_width = 80;
        tick_per_char = (t_range + 79) / 80;
    } else {
        char_width = 80;
        tick_per_char = (t_range + 79) / 80;
    }

    printk("tick:");
    int col = 5;
    for (int c = 0; c < char_width; c += 10) {
        uint t = t_min + c * tick_per_char;
        while (col < c) { printk(" "); col++; }
        int ndig = 1;
        uint tmp = t;
        while (tmp >= 10) { ndig++; tmp /= 10; }
        printk("%d", t);
        col += ndig;
    }
    printk("\n");
    printk("     ");
    for (int c = 0; c < char_width; c++) {
        if (c % 10 == 0) {
            if (c % 50 == 0)
                printk("|");
            else
                printk("'");
        } else {
            printk(" ");
        }
    }
    printk("\n");

    for (int pi = 0; pi < pid_count; pi++) {
        int pid = pids[pi];
        char *pname = pid_names[pi];

        if (pid < 10)
            printk("pid0%d ", pid);
        else
            printk("pid%d ", pid);

        for (int c = 0; c < char_width; c++) {
            uint t_start = t_min + c * tick_per_char;
            uint t_end = t_start + tick_per_char - 1;
            if (t_end > t_max)
                t_end = t_max;

            int has_holding = 0;
            int has_waiting = 0;

            for (int e = 0; e < event_count; e++) {
                if (events[e].pid != pid)
                    continue;

                if (events[e].type == SEM_EVENT_ACQUIRE) {
                    uint acq_tick = events[e].ticks;
                    uint rel_tick = t_max + 1;
                    for (int r = e + 1; r < event_count; r++) {
                        if (events[r].pid == pid &&
                            events[r].type == SEM_EVENT_RELEASE) {
                            rel_tick = events[r].ticks;
                            break;
                        }
                    }
                    // 持有区间: [acq_tick, rel_tick)
                    if (t_start < rel_tick && t_end >= acq_tick) {
                        has_holding = 1;
                    } else if (acq_tick == rel_tick &&
                               t_start == acq_tick && t_end == acq_tick) {
                        has_holding = 1;
                    }
                } else if (events[e].type == SEM_EVENT_WAIT) {
                    uint wait_tick = events[e].ticks;
                    uint acq_tick = t_max + 1;
                    for (int a = e + 1; a < event_count; a++) {
                        if (events[a].pid == pid &&
                            events[a].type == SEM_EVENT_ACQUIRE) {
                            acq_tick = events[a].ticks;
                            break;
                        }
                    }
                    // 等待区间: [wait_tick, acq_tick)
                    if (t_start < acq_tick && t_end >= wait_tick) {
                        has_waiting = 1;
                    } else if (wait_tick == acq_tick &&
                               t_start == wait_tick && t_end == wait_tick) {
                        has_waiting = 1;
                    }
                }
            }

            if (has_holding)
                printk("#");
            else if (has_waiting)
                printk("W");
            else
                printk(".");
        }
        printk(" %s\n", pname);
    }
    printk("\n");
}

