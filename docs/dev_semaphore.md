# 有关信号量开发的细节

## 信号量
(省略: 信号量的介绍, 作用等)

Dijkstra 原始语义信号量 P, V 操作:

```text
P() {
    value--;
    if (value < 0): 
        block();
}

```

```text
V() {
    value++;
    if (value <= 0): 
        wakeup();
}
```

此处 `<=0` 而不是 `<0` 是因为 `V` 操作会使计数 $+1$, 
因此有进程在等待信号量资源的判断条件应该是 `>=0` 
(最少等待即只有一个进程等待的情况s, 计数是 -1)

而在操作系统中实现需要了解 xv6 的进程操作, 找到 block() 和 wakup() 的实际替代.

另外, 由于涉及到信号量计数(共享状态)的修改, P V 操作应该都在临界区内, 需要上锁.

## xv6 的进程阻塞与唤醒

xv6 没有进程等待队列, 而是使用 `void *chan` 判断是否应该唤醒/是否是等待统一资源的进程/

因此无法在信号量中直接加入一条等待队列, 
使 wakeup 时可以在等待队列中寻找一个进程唤醒,
所以 wakeup 时直接传入 semaphore 的锁(前文提及)

xv6 的 wakeup 如下:

```c
// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}
```

(省略对此代码的介绍)

实际开发中发现, xv6 只有唤醒全部等待 `chan` 的 `wakeup`,
这样会导致惊群效应和 dijkstra 语义信号量的 bug,

于是添加 `wakup_one`:

```c
void
wakeup_one(void *chan) {
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        release(&p->lock);
        break;
      }
      release(&p->lock);
    }
  }
}
```

在第一次找到阻塞中且等待当前 chan 的进程时就释放锁并退出.


阻塞同理:

```c
// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}
```

将当前进程状态改为阻塞并重新调度

## 信号量实现

信号量的结构, 包括计数, 自旋锁, 状态和名称
```c
struct semaphore {
  int count;
  struct spinlock lock;
  int active;
  char name[16];
};
````

对 P V 操作的实现

(record_event 为后续 gantt 图记录事件信息, 
to gangzi: 这个地方也可以在文档中先只写不带 record 的核心代码, 然后后面提到 gantt 图再写一个全的, 凑字数(雾):

```c
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
```

## 系统调用适配

按照 POSIX 的调用方式, 每个信号量的s标识是 `sem_id`, 因此需要全局的信号量表来记录.

```c
struct semaphore sem_table[NSEM];
struct spinlock sem_table_lock;   //保护sem_table分配的自旋锁

//初始化一个 signal chovy    --Fuyeorr
//你们signal给我signal好了啊    --折鸦の夜明前 (注: 这句话是 yuanhao 写的, 不信可以查 commit history)
void
sem_table_init(void)   //初始化整个表
{
    for (int i = 0; i < NSEM; i++) {
        sem_init(&sem_table[i], 0);
    }
    initlock(&sem_table_lock, "semtab");
    sem_event_log_init();
}

//分配一个 signal chovy
int
sem_alloc(int value, char *name)
{
  acquire(&sem_table_lock);
  for (int i = 0; i < NSEM; i++)
    {
      if (sem_table[i].active == 0)
      {
        sem_table[i].active = 1;
        sem_table[i].count = value;      //设置初始资源数
        safestrcpy(sem_table[i].name, name, sizeof(sem_table[i].name));
        release(&sem_table_lock);
        return i;
      }
    }
  release(&sem_table_lock);
  return -1;
}

//释放一个 signal chovy
void
sem_free(int id)
{
  if (id < 0 || id >= NSEM)
    return;
  acquire(&sem_table_lock);
  wakeup(&sem_table[id]);         //我chovy的都给我起来
  sem_table[id].active = 0;
  memset(sem_table[id].name, 0, 16);
  release(&sem_table_lock);
}

```

初始化:

```c
void
sem_init(struct semaphore *sem, int value)
{
    sem->count = value;
    initlock(&sem->lock, "semaphore");
    sem->active = 0;
    memset(sem->name, 0, 16);
}

```


系统调用添加:
```c
// syscall.c
extern uint64 sys_sem_open(void);
extern uint64 sys_sem_wait(void);
extern uint64 sys_sem_signal(void);
extern uint64 sys_sem_close(void);
extern uint64 sys_sem_gantt(void);

static uint64 (*syscalls[])(void) = {
  ... (这段 xv6 把 format 关了没绷住, 好像不是标准C)
  [SYS_sem_open]   sys_sem_open,
  [SYS_sem_wait]   sys_sem_wait,
  [SYS_sem_signal] sys_sem_signal,
  [SYS_sem_close]  sys_sem_close,
  [SYS_sem_gantt]  sys_sem_gantt,
}

// syscall.h
#define SYS_sem_open   24
#define SYS_sem_wait   25
#define SYS_sem_signal 26
#define SYS_sem_close  27
#define SYS_sem_gantt  28
```


## gantt 图

应 **某位老师** 的要求, 添加**运行时记录时间信息的甘特图**.

**尽管这会使调度不太准确, 而且 xv6 的 100ms tick 也使得统计没什么大的价值**, 

但是我**完全**认同老师的专业水平, 理解老师希望我们实践更多的良苦用心, 在这里感谢老师的栽培!


记录事件相关信息, 尤其是实现类型. 此外还包括是哪个信号量, 哪个进程的事件, 在哪个 tick

```c
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
```

在特定时间记录事件:
```c
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
```

(在这里贴一下之前说的 gantt 图记录的过程, `record_event` 那些)
```c
your text here
```

通过记录的信息, 绘制g antt 图的系统调用: 

请输入文本:

```cpp
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
```
