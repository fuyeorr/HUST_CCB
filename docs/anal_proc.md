# xv6 操作系统的进程管理分析

## 一、xv6 进程管理机制的特点

xv6 的进程管理设计简洁但完整实现了 Unix 进程模型的核心概念，包括进程创建、调度、终止和回收等。以下结合源码分析其主要特点。

### 1. 进程控制块（PCB）：`struct proc`

每个进程由一个 `struct proc` 结构体表示，存储在内核的全局数组 `proc[NPROC]` 中（最多 `NPROC=64` 个进程）。

源码参考：`kernel/proc.h:83-105` 和 `kernel/proc.c:12`。

```c
// kernel/proc.h:80
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// kernel/proc.h:83-105
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state; // Process state
  void *chan;           // If non-zero, sleeping on chan
  int killed;           // If non-zero, have been killed
  int xstate;           // Exit status to be returned to parent's wait
  int pid;              // Process ID

  // wait_lock must be held when using this:
  struct proc *parent; // Parent process

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
};
```

> **注意：** 此版本新增了 `USED` 状态（原版 xv6 称为 `EMBRYO`），并扩展了内核信号量 `sem_table[NSEM]`（`kernel/proc.h:30-31`）。

### 2. 进程状态转换

进程在其生命周期中经历六种状态的转换：

```
 allocproc()       scheduler()       yield()/sleep()
  UNUSED ──→ USED ──→ RUNNABLE ──→ RUNNING ──→ RUNNABLE
                        ↑              │
                        │              ↓
                   wakeup()        SLEEPING  (等待 I/O)
                                       │
                                  kexit()↓
                                     ZOMBIE ──→ UNUSED (被父进程 kwait 回收)
```

状态说明（`kernel/proc.h:80`）：

| 状态 | 含义 |
|------|------|
| `UNUSED` | 空闲 PCB，可分配 |
| `USED` | 已分配，尚未就绪 |
| `SLEEPING` | 阻塞，等待在某个 chan 上 |
| `RUNNABLE` | 就绪，等待 CPU 调度 |
| `RUNNING` | 当前正在运行 |
| `ZOMBIE` | 已退出，等待父进程回收 |

---

## 二、核心系统调用分析

### 1. `fork()` —— 进程创建

`fork()` 创建一个与父进程几乎完全相同的子进程，是 xv6 中创建进程的唯一方式。

**调用链：** `fork()` (用户态) → `sys_fork()` → `kfork()`。

源码参考：`kernel/proc.c:308-355` 中的 `kfork` 函数。

```c
// kernel/proc.c:308-355
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // ① Allocate process (allocproc)
  if((np = allocproc()) == 0) {
    return -1;
  }

  // ② Copy user memory from parent to child
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // ③ copy saved user registers
  *(np->trapframe) = *(p->trapframe);

  // ④ Cause fork to return 0 in the child
  np->trapframe->a0 = 0;

  // ⑤ increment reference counts on open file descriptors
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;      // ⑥ 建立父子关系
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE; // ⑦ 子进程就绪，等待调度
  release(&np->lock);

  return pid;           // 父进程返回子进程 PID
}
```

**关键步骤分析：**

1. **`allocproc()`（`kernel/proc.c:158-198`）**：遍历 `proc[]` 找到 `UNUSED` 状态的 PCB，分配 pid、trapframe 页和用户页表。初始化内核上下文 `context.ra = forkret`，使得子进程首次被调度时从 `forkret` 开始执行。

2. **`uvmcopy()`**：逐页复制父进程的虚拟地址空间，子进程获得独立的物理内存副本（非写时拷贝）。

3. **`*(np->trapframe) = *(p->trapframe)`**：复制用户态寄存器快照，子进程将从 fork 调用后的下一条指令开始执行。

4. **`np->trapframe->a0 = 0`**：这是 fork 实现的最关键一行——将子进程的返回值寄存器 `a0` 设为 0，使得子进程中 `fork()` 返回 0，而父进程中返回 `pid`。

5. **文件描述符共享**：通过 `filedup` 增加文件引用计数，父子进程共享同一文件偏移指针。

6. **建立父子关系**：`np->parent = p`，在 `wait_lock` 保护下完成，防止 `wait()` 中丢失唤醒。

7. **设置 RUNNABLE**：子进程创建完毕，加入调度队列。

### 2. `allocproc()` —— PCB 分配与子进程"出生点"

源码参考：`kernel/proc.c:158-198`。

```c
// kernel/proc.c:158-198
static struct proc *
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}
```

> **关键设计：** `p->context.ra = (uint64)forkret` 将子进程的返回地址设为 `forkret`。当调度器第一次 `swtch` 到该子进程时，`swtch` 恢复 `context` 中的寄存器，最后 `ret` 指令跳转到 `forkret`（`kernel/proc.c:554-587`）。`forkret` 中会执行文件系统初始化（仅首次），然后调用 `kexec("/init")` 启动第一个用户进程或直接返回用户空间。

### 3. `exit()` —— 进程退出

源码参考：`kernel/proc.c:376-415` 中的 `kexit` 函数。

```c
// kernel/proc.c:376-415
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // ① Close all open files
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);       // ② 释放当前目录引用
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  reparent(p);        // ③ 子进程托付给 init

  wakeup(p->parent);  // ④ 唤醒可能在 wait() 的父进程

  acquire(&p->lock);

  p->xstate = status; // ⑤ 保存退出状态
  p->state = ZOMBIE;  // ⑥ 进入僵尸状态

  release(&wait_lock);

  sched();            // ⑦ 放弃 CPU，永不再返回
  panic("zombie exit");
}
```

**关键步骤分析：**

1. **关闭所有文件**：遍历 `ofile[]`，调用 `fileclose` 递减引用计数。
2. **释放当前目录**：`iput` 递减 cwd inode 的引用计数。
3. **`reparent()`（`kernel/proc.c:359-370`）**：遍历进程表，将本进程的所有子进程的 `parent` 改为 `initproc`，确保它们不会成为孤儿。
4. **唤醒父进程**：如果父进程正在 `wait()` 中睡眠，将其唤醒。
5. **设置退出状态**：将 `status` 存入 `p->xstate`，供父进程的 `wait()` 读取。
6. **进入 ZOMBIE**：进程进入僵尸状态，保留 PCB 等待父进程回收。
7. **调用 `sched()`**：切换到调度器，且永不再返回。

### 4. `wait()` —— 等待子进程退出

源码参考：`kernel/proc.c:419-464` 中的 `kwait` 函数。

```c
// kernel/proc.c:419-464
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        acquire(&pp->lock);
        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0){
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);    // 释放 PCB 及其资源
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }

    sleep(p, &wait_lock); // 在 wait_lock 上睡眠，等待子进程唤醒
  }
}
```

**关键步骤分析：**

1. **遍历进程表**：查找状态为 `ZOMBIE` 的子进程。
2. **获取退出状态**：通过 `copyout` 将子进程的 `xstate` 复制到用户空间的 `addr` 地址。
3. **`freeproc()`（`kernel/proc.c:204-220`）**：释放子进程的 trapframe、页表和内核栈，将状态重置为 `UNUSED`。
4. **睡眠等待**：如果没有已退出的子进程但还有活着的子进程，调用 `sleep(p, &wait_lock)` 在 `wait_lock` 上睡眠，直到子进程退出时通过 `wakeup(p->parent)` 唤醒。

### 5. `kill()` —— 终止进程

源码参考：`kernel/proc.c:657-676` 中的 `kkill` 函数。

```c
// kernel/proc.c:657-676
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        p->state = RUNNABLE;  // 若目标在睡眠，直接唤醒
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
```

> **注意：** `kkill` 只设置 `killed=1`，不直接终止进程。目标进程在从内核态返回用户态时（`usertrap` 中）检查 `killed` 标志，若为真则调用 `kexit(-1)`。如果目标在 `SLEEPING` 状态，则将其设为 `RUNNABLE` 使其被调度后能检测到 killed 标志。

---

## 三、进程调度机制

### 1. 调度器 `scheduler()`

源码参考：`kernel/proc.c:473-512`。

```c
// kernel/proc.c:473-512
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE){
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);
        // Process is done running for now.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0){
      intr_on();
      asm volatile("wfi");
      intr_off();
    }
  }
}
```

**关键机制：**

- **轮询调度（Round-Robin）**：遍历 `proc[]`，找到第一个 `RUNNABLE` 进程。
- **`swtch(&c->context, &p->context)`（`kernel/swtch.S`）**：保存当前调度器上下文到 `c->context`，恢复目标进程的 `p->context`，完成上下文切换。`swtch` 保存全部 callee-saved 寄存器（`ra, sp, s0~s11`）。
- **`wfi` 指令**：当没有可运行的进程时，CPU 进入低功耗等待状态，由中断唤醒。
- **锁的协作**：进程持有 `p->lock` 进入 `sched()`（`kernel/proc.c:521-539`），调度器在 `swtch` 返回后释放 `p->lock`。

### 2. 上下文切换流程

```
用户进程 A (forkdemo)              调度器 (scheduler)            用户进程 B (sh)
────────────────────────          ──────────────────          ────────────────
  系统调用/中断/timer                for(;;){
    → usertrap()                       遍历 proc[]
    → yield()                          找到 RUNNABLE
      p->state = RUNNABLE               p->state = RUNNING
      sched()                           swtch(&c->context, &p->context) ──→ 恢复 B 的 context
        swtch(&p->context, &c->context) ──→ 保存 A 到 c->context, 返回 scheduler
                                        ...                              B 运行中...
                                        ←── swtch(&c->context, &p->context) (B yield/sleep/exit)
                                        release(&p->lock)
                                    遍历下一个进程...
                                    找到 A (RUNNABLE)
                                    swtch(&c->context, &p->context) ──→ 恢复 A 的 context
                                                                        A 继续运行...
```

### 3. `sleep()` 与 `wakeup()` 机制

xv6 使用"通道（chan）"概念实现睡眠/唤醒，`chan` 是任意内核地址，用作等待条件。

源码参考：`kernel/proc.c:591-636`。

```c
// kernel/proc.c:591-618
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  release(lk);         // 释放条件锁

  p->chan = chan;      // 记录等待通道
  p->state = SLEEPING; // 设置睡眠状态

  sched();             // 切换到调度器

  p->chan = 0;         // 被唤醒后清理

  release(&p->lock);
  acquire(lk);         // 重新获取条件锁
}

// kernel/proc.c:622-636
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan){
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}
```

> **关键设计：** `sleep` 在设置 `chan` 和 `SLEEPING` 状态时持有 `p->lock`，`wakeup` 修改状态时也持有 `p->lock`，这保证了不会丢失唤醒信号。即：如果在 `sleep` 释放 `lk` 之后、进入 `SLEEPING` 之前，`wakeup` 被调用，`wakeup` 将因为拿不到 `p->lock` 而阻塞，直到 `sleep` 完成状态设置并调用 `sched()`。

---

## 四、forkdemo.c 代码功能原理分析

`user/forkdemo.c` 包含 6 个测试用例，覆盖 xv6 进程管理的核心场景。

### 测试 1：基本 fork + wait

```c
void demo_fork(void)
{
  int pid;
  printf("Parent PID = %d, about to fork...\n", getpid());

  pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    return;
  }

  if(pid == 0){
    printf("  [Child ] PID=%d, fork() returned=%d\n", getpid(), pid);
    exit(7);   // 子进程以状态码 7 退出
  } else {
    printf("  [Parent] fork() returned child PID=%d\n", pid);
    int status;
    int wpid = wait(&status);
    printf("  [Parent] wait() returned PID=%d, exit status=%d\n", wpid, status);
  }
}
```

**原理：**

1. `fork()` 调用 `sys_fork → kfork`（`kernel/proc.c:308`）。`allocproc` 创建子进程 PCB，`uvmcopy` 复制地址空间，`np->trapframe->a0 = 0` 使子进程中 `fork()` 返回 0。
2. 子进程执行 `exit(7)` → `kexit(7)`（`kernel/proc.c:376`），关闭文件、将状态写入 `p->xstate = 7`，设为 `ZOMBIE`，唤醒父进程后调用 `sched()`。
3. 父进程 `wait(&status)` → `kwait(addr)`（`kernel/proc.c:419`），遍历进程表找到 `ZOMBIE` 子进程，通过 `copyout` 将 `pp->xstate`（值为 7）写入用户空间 `&status`，然后 `freeproc` 回收子进程资源。输出 `exit status=7` 验证状态码传递正确。

### 测试 2：父子进程并发运行

```c
void demo_concurrent(void)
{
  int pid = fork();
  if(pid == 0){
    for(int i = 0; i < 3; i++){
      printf("  [Child ] tick %d\n", i);
      pause(20);   // 休眠 20 个时钟滴答，主动让出 CPU
    }
    exit(0);
  } else {
    for(int i = 0; i < 3; i++){
      printf("  [Parent] tick %d\n", i);
      pause(20);
    }
    wait(0);
  }
}
```

**原理：**

- `pause(20)` 调用 `sys_pause`（`kernel/sysproc.c:68-88`），在 `tickslock` 保护的 `&ticks` 通道上 `sleep`，每当时钟中断到来（`trap.c:186`），`wakeup(&ticks)` 唤醒休眠进程。
- 父子进程交替打印，体现 xv6 调度器的轮询调度机制——每次 `pause` 都调用 `sched()` 让出 CPU，调度器选择另一个 `RUNNABLE` 进程运行。

### 测试 3：多个子进程

```c
void demo_multi_children(void)
{
  int i, pid;
  for(i = 0; i < 4; i++){
    pid = fork();
    if(pid == 0){
      printf("  [Child #%d] PID=%d started\n", i, getpid());
      pause(30 * (i + 1));  // 不同子进程休眠时间不同
      exit(i);               // 退出码为子进程编号
    }
    printf("  [Parent  ] spawned child #%d PID=%d\n", i, pid);
  }

  int status;
  while((pid = wait(&status)) > 0){
    printf("  [Parent  ] child PID=%d exited with status=%d\n", pid, status);
  }
}
```

**原理：**

- 父进程在一个循环中连续 `fork` 4 次，每次创建的子进程都在 `if(pid==0)` 分支中进入子进程逻辑，不会返回父进程循环。`fork()` 返回 0 和 pid 的差异保证了父子分叉。
- 父进程调用 `while((pid=wait(&status))>0)` 循环回收所有子进程。当 `wait()` 返回 -1（没有子进程）时退出循环。每次回收到的 `status` 值与子进程传入 `exit(i)` 的参数一致。

### 测试 4：僵尸进程

```c
void demo_zombie(void)
{
  int pid = fork();
  if(pid == 0){
    printf("  [Child ] PID=%d, exiting immediately\n", getpid());
    exit(0);
  } else {
    printf("  [Parent] sleeping 100 ticks (child is zombie)...\n");
    pause(100);   // 父进程延迟回收
    printf("  [Parent] now calling wait() to reap zombie\n");
    wait(0);
  }
}
```

**原理：**

- 子进程先调用 `exit(0)`，进入 `ZOMBIE` 状态。此时其 PCB 和资源尚未释放，形成僵尸进程。
- 父进程 `pause(100)` 期间，子进程处于僵尸态——如果此时运行 `ps`（Ctrl+P），可以看到该僵尸进程。
- 父进程调用 `wait(0)` 后，在 `kwait` 中找到 `ZOMBIE` 子进程，调用 `freeproc` 释放其 PCB，僵尸被回收。

### 测试 5：孤儿进程

```c
void demo_orphan(void)
{
  int pid = fork();
  if(pid == 0){
    int pid2 = fork();
    if(pid2 == 0){
      printf("  [Grandchild] parent PID=%d\n", getpid());
      pause(50);
      printf("  [Grandchild] exiting (should be adopted by init)\n");
      exit(0);
    }
    printf("  [Child] exiting, grandchild becomes orphan\n");
    exit(0);  // 父进程（Child）先退出
  } else {
    wait(0);  // 最外层父进程只等待 Child
    printf("  [Parent] grandchild was reaped by init\n");
  }
}
```

**原理：**

- 最外层进程 fork 出 Child，Child 又 fork 出 Grandchild。然后 Child 退出，Grandchild 变成孤儿。
- Child 退出时调用 `kexit`（`kernel/proc.c:376`），其中的 `reparent()`（`kernel/proc.c:359-370`）将 Child 的所有子进程（即 Grandchild）的 `parent` 改为 `initproc`。
- `init` 进程（`user/init.c:26-53`）在无限循环中调用 `wait(0)`，会回收被托付给它的 Grandchild 僵尸进程。因此 Grandchild 虽然成为孤儿，但其资源仍能被正常回收，不会造成内存泄漏。

### 测试 6：fork + exec

```c
void demo_exec(void)
{
  int pid = fork();
  if(pid == 0){
    char *args[] = {"echo", "Hello from exec!", 0};
    exec("echo", args);  // 子进程替换为 echo 程序
    printf("exec failed!\n");
    exit(1);
  } else {
    wait(0);
  }
}
```

**原理：**

- `fork()` 创建父进程的副本后，子进程调用 `exec("echo", args)` → `sys_exec`（`kernel/sysfile.c`）→ `kexec`（`kernel/exec.c`）。
- `kexec` 读取 ELF 可执行文件（`/echo`），创建新的页表，将代码段和数据段加载到内存，设置新的入口点，释放旧页表。子进程的地址空间被完全替换为 `echo` 程序。
- `echo` 输出 "Hello from exec!" 后调用 `exit(0)`，父进程 `wait(0)` 回收。
- 这是 Unix shell 实现命令执行的基础模式：`fork` 创建子进程，`exec` 替换子进程为新程序。

---

## 五、总结

xv6 的进程管理系统以 `struct proc` 为核心，实现了完整的进程生命周期管理：

1. **进程创建（`kfork`）**：通过 `allocproc` 分配 PCB，`uvmcopy` 复制地址空间，`trapframe->a0 = 0` 实现父子进程返回值差异化，是 Unix `fork` 语义的经典实现。

2. **进程终止（`kexit`）**：通过 `reparent()` 防止孤儿进程，进入 `ZOMBIE` 状态等待父进程回收，体现了 Unix 的"先死后葬"哲学。

3. **进程回收（`kwait`）**：遍历进程表查找 `ZOMBIE` 子进程，利用 `sleep/wakeup` 机制实现阻塞等待，`copyout` 将退出状态传递至用户空间。

4. **进程调度（`scheduler`）**：采用简单的轮询算法，通过 `swtch` 实现上下文切换，`wfi` 指令实现空闲等待。

5. **同步机制（`sleep/wakeup`）**：基于通道地址的睡眠/唤醒，通过锁的协作保证不丢失唤醒信号。

6. **编程验证（`forkdemo`）**：6 个测试用例覆盖 fork/wait/exit 的基本使用、并发调度、多子进程管理、僵尸进程、孤儿进程托付、以及 fork+exec 经典模式，完整验证了 xv6 进程管理的核心机制。

整体上 xv6 进程管理的设计在有限代码量内（`proc.c` 约 757 行）实现了完整的 Unix 进程模型，数据结构清晰（`proc`/`cpu`/`context`/`trapframe`），锁的分层严谨（`wait_lock` → `p->lock` → `tickslock`），是学习操作系统进程管理的优秀范例。
