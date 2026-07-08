# xv6 操作系统的进程管理分析

## 一、进程控制块（PCB）与状态

每个进程由一个 `struct proc` 结构体表示，存储在内核全局数组 `proc[NPROC]` 中（最多 `NPROC=64` 个进程）。

源码参考：`kernel/proc.h:83-105` 和 `kernel/proc.c:12`。

核心字段：

| 字段 | 含义 |
|------|------|
| `state` | 进程状态（`UNUSED/USED/SLEEPING/RUNNABLE/RUNNING/ZOMBIE`） |
| `pid` | 进程 ID |
| `parent` | 父进程指针（在 `wait_lock` 保护下使用） |
| `kstack` / `pagetable` | 内核栈和用户页表 |
| `trapframe` | 用户态寄存器快照（`a0` 即 fork 返回值） |
| `context` | 内核上下文（`ra`/`sp`/callee-saved 寄存器），供 `swtch` 使用 |
| `ofile[]` / `cwd` | 打开文件表和当前目录 |
| `xstate` | 退出状态，供父进程 `wait()` 读取 |
| `killed` | 标记是否被 `kill()` |

状态转换图：

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

**调用链：** `fork()` (用户态) → `sys_fork()` → `kfork()`。

源码参考：`kernel/proc.c:308-355`。

**关键步骤：**

1. **`allocproc()`**（`kernel/proc.c:158-198`）：遍历 `proc[]` 找到 `UNUSED` PCB，分配 pid、trapframe 页和用户页表。设置 `context.ra = forkret`，使子进程首次被调度时从 `forkret` 开始执行。

2. **`uvmcopy()`**：逐页复制父进程的虚拟地址空间，子进程获得独立的物理内存副本（非写时拷贝）。

3. **`*(np->trapframe) = *(p->trapframe)`**：复制用户态寄存器快照，子进程将从 fork 调用后的下一条指令开始执行。

4. **`np->trapframe->a0 = 0`**：将子进程的返回值寄存器 `a0` 设为 0，使得子进程中 `fork()` 返回 0，而父进程中返回 `pid`。这是 fork 实现的最关键一行。

5. **文件描述符共享**：通过 `filedup` 增加文件引用计数，父子进程共享同一文件偏移指针。

6. **建立父子关系**：`np->parent = p`，在 `wait_lock` 保护下完成。

7. **设置 RUNNABLE**：子进程创建完毕，加入调度队列。

### 2. `exit()` —— 进程退出

源码参考：`kernel/proc.c:376-415` 中的 `kexit`。

**关键步骤：**

1. 关闭所有文件描述符，释放当前目录引用。
2. **`reparent()`**（`kernel/proc.c:359-370`）：遍历进程表，将本进程的所有子进程的 `parent` 改为 `initproc`，确保没有孤儿进程。
3. **`wakeup(p->parent)`**：唤醒可能在 `wait()` 中阻塞的父进程。
4. 设置 `p->xstate = status`，状态设为 `ZOMBIE`，进入僵尸态等待父进程回收。
5. 调用 `sched()` 放弃 CPU，永不再返回。

### 3. `wait()` —— 等待子进程退出

源码参考：`kernel/proc.c:419-464` 中的 `kwait`。

**关键步骤：**

1. 遍历进程表，查找状态为 `ZOMBIE` 的子进程。
2. 通过 `copyout` 将子进程的 `xstate` 复制到用户空间的 `addr` 地址。
3. **`freeproc()`**（`kernel/proc.c:204-220`）：释放子进程的 trapframe、页表和内核栈，PCB 重置为 `UNUSED`。
4. 如果没有已退出的子进程但还有活着的子进程，调用 `sleep(p, &wait_lock)` 阻塞，直到子进程退出时通过 `wakeup(p->parent)` 唤醒。

### 4. `kill()` —— 终止进程

源码参考：`kernel/proc.c:657-676` 中的 `kkill`。

只设置 `killed=1`，不直接终止进程。目标进程在从内核态返回用户态时（`usertrap` 中）检查 `killed` 标志，若为真则调用 `kexit(-1)`。如果目标在 `SLEEPING` 状态，则将其设为 `RUNNABLE` 使其被调度后能检测到 killed 标志。

---

## 三、进程调度机制

### 1. 调度器（Round-Robin）

源码参考：`kernel/proc.c:473-512`。

遍历 `proc[]` 找到第一个 `RUNNABLE` 进程，设为 `RUNNING`，通过 `swtch(&c->context, &p->context)`（`kernel/swtch.S`）完成上下文切换。无就绪进程时执行 `wfi` 指令等待中断。

### 2. 上下文切换流程

```
用户进程 A                  调度器 (scheduler)              用户进程 B
──────────────             ──────────────────             ──────────────
系统调用/中断/timer         for(;;){
  → yield()                   遍历 proc[]
    sched()                   找到 RUNNABLE
      swtch(&p->context,      swtch(&c->context, &p->context) ──→ 恢复 B 继续运行
             &c->context) ──→ 保存 A, 返回 scheduler
                              ...                          B 运行中...
                              ←── swtch(&c->context, &p->context) (B yield/exit)
                              继续遍历...
                              找到 A, swtch →                A 继续运行...
```

`sleep()` 与 `wakeup()` 协同工作，通过 `p->lock` 保证不会丢失唤醒信号：`sleep` 持有 `p->lock` 设置 `chan` 和 `SLEEPING`，`wakeup` 修改状态时也持有 `p->lock`，形成临界区保护。

---

## 四、forkdemo.c 测试用例代码分析

以下 6 个测试用例覆盖 xv6 进程管理的核心场景，源码来自 `xv6-riscv/user/forkdemo.c`。

---

### 1. 基本 fork/wait —— `demo_fork()`

```c
void demo_fork(void)
{
  int pid;

  printf("\n=== 1. Basic fork() demo ===\n");
  printf("Parent PID = %d, about to fork...\n", getpid());

  pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    return;
  }

  if (pid == 0) {
    printf("  [Child ] PID=%d, fork() returned=%d\n", getpid(), pid);
    printf("  [Child ] doing some work...\n");
    exit(7);
  } else {
    printf("  [Parent] fork() returned child PID=%d\n", pid);
    printf("  [Parent] waiting for child...\n");
    int status;
    int wpid = wait(&status);
    printf("  [Parent] wait() returned PID=%d, exit status=%d\n", wpid, status);
  }
}
```

| 代码行 | 验证的 xv6 内核机制 |
|--------|-------------------|
| `pid = fork()` | `kfork()` 调用 `allocproc()` 分配 PCB + `uvmcopy()` 复制地址空间 |
| `pid == 0` 分支 | `np->trapframe->a0 = 0` 使子进程返回 0，父进程返回子进程 pid |
| `exit(7)` | `kexit()` 将状态码写入 `p->xstate`，进程状态置为 `ZOMBIE` |
| `wait(&status)` | `kwait()` 遍历进程表查找 `ZOMBIE` 子进程，`copyout` 传递退出状态，`freeproc()` 释放资源 |

---

### 2. 父子并发 —— `demo_concurrent()`

```c
void demo_concurrent(void)
{
  printf("\n=== 2. Concurrent parent/child demo ===\n");
  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    return;
  }
  if (pid == 0) {
    for (int i = 0; i < 3; i++) {
      printf("  [Child ] tick %d\n", i);
      pause(20);               // 让出 CPU 20 ticks
    }
    exit(0);
  } else {
    for (int i = 0; i < 3; i++) {
      printf("  [Parent] tick %d\n", i);
      pause(20);               // 让出 CPU 20 ticks
    }
    wait(0);
  }
}
```

| 代码行 | 验证的 xv6 内核机制 |
|--------|-------------------|
| `pause(20)` | 调用 `sleep()` 让出 CPU，调度器切换到另一个 `RUNNABLE` 进程 |
| 父子交替打印 | 验证轮询（Round-Robin）调度策略，父子进程共享 CPU 时间片 |
| `wait(0)` | 父进程在子进程退出后回收，若子进程未退出则 `sleep` 阻塞等待 |

**预期输出**显示父子进程的 tick 日志交替出现，说明调度器在两者之间来回切换。

---

### 3. 多个子进程 —— `demo_multi_children()`

```c
void demo_multi_children(void)
{
  printf("\n=== 3. Multiple children demo ===\n");
  int i, pid;

  for (i = 0; i < 4; i++) {
    pid = fork();
    if (pid < 0) {
      printf("  fork failed at i=%d\n", i);
      break;
    }
    if (pid == 0) {
      printf("  [Child #%d] PID=%d started\n", i, getpid());
      pause(30 * (i + 1));
      printf("  [Child #%d] PID=%d exiting\n", i, getpid());
      exit(i);
    }
    printf("  [Parent  ] spawned child #%d PID=%d\n", i, pid);
  }

  printf("  [Parent  ] waiting for all children...\n");
  int status;
  while ((pid = wait(&status)) > 0) {
    printf("  [Parent  ] child PID=%d exited with status=%d\n", pid, status);
  }
  printf("  [Parent  ] no more children, wait()=%d\n", pid);
}
```

| 代码行 | 验证的 xv6 内核机制 |
|--------|-------------------|
| `for` 循环 `fork()` 4 次 | `kfork()` 创建 4 个子进程，各分配独立的 PCB 和地址空间 |
| `exit(i)` | 每个子进程以不同序号退出，验证 `xstate` 能正确传递退出码 |
| `while ((pid = wait(&status)) > 0)` | `kwait()` 每次回收一个已退出的子进程，返回其 pid；当无子进程时返回 -1 终止循环 |
| 子进程休眠时间不同 | 验证 `sleep/wakeup` 与调度器的协作：子进程在不同时刻进入 `RUNNABLE` 并依次退出 |

---

### 4. 僵尸进程 —— `demo_zombie()`

```c
void demo_zombie(void)
{
  printf("\n=== 4. Zombie process demo ===\n");
  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    return;
  }
  if (pid == 0) {
    printf("  [Child ] PID=%d, exiting immediately\n", getpid());
    exit(0);
  } else {
    printf("  [Parent] PID=%d, child PID=%d\n", getpid(), pid);
    printf("  [Parent] sleeping 100 ticks (child is zombie)...\n");
    pause(100);
    printf("  [Parent] now calling wait() to reap zombie\n");
    wait(0);
    printf("  [Parent] zombie reaped\n");
  }
}
```

| 代码行 | 验证的 xv6 内核机制 |
|--------|-------------------|
| 子进程立即 `exit(0)` | `kexit()` 将子进程状态设为 `ZOMBIE`，释放文件描述符和用户内存，但保留 PCB |
| `pause(100)` 父进程延迟回收 | 子进程保持僵尸态 100 ticks，验证 PCB 在 `ZOMBIE` 状态下未被释放 |
| `wait(0)` | `kwait()` 找到 `ZOMBIE` 子进程，调用 `freeproc()` 释放 trapframe、页表、内核栈，PCB 重置为 `UNUSED` |

**关键观察**：在父进程 `pause(100)` 期间，子进程的 PCB 条目（可通过 `ps` 或 `ctrl-p` 查看）状态显示为 `ZOMBIE`，直到 `wait()` 调用后彻底消失。

---

### 5. 孤儿进程 —— `demo_orphan()`

```c
void demo_orphan(void)
{
  printf("\n=== 5. Orphan process demo ===\n");
  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    return;
  }
  if (pid == 0) {
    int pid2 = fork();
    if (pid2 < 0) {
      printf("  fork2 failed\n");
      exit(1);
    }
    if (pid2 == 0) {
      printf("  [Grandchild] PID=%d, I am an orphan now\n", getpid());
      printf("  [Grandchild] sleeping 50 ticks...\n");
      pause(50);
      printf("  [Grandchild] exiting (should be adopted by init)\n");
      exit(0);
    }
    printf("  [Child] PID=%d exiting, grandchild becomes orphan\n", getpid());
    exit(0);
  } else {
    printf("  [Parent] PID=%d, child PID=%d\n", getpid(), pid);
    int status;
    wait(&status);
    printf("  [Parent] child exited, status=%d\n", status);
    printf("  [Parent] grandchild was reaped by init\n");
  }
}
```

| 代码行 | 验证的 xv6 内核机制 |
|--------|-------------------|
| Child 调用 `fork()` 创建 Grandchild | 验证 `kfork()` 创建的嵌套父子关系 |
| Child 调用 `exit(0)` 先于 Grandchild | `kexit()` 中调用 `reparent()`，将 Grandchild 的 `parent` 改为 `initproc` |
| Grandchild `pause(50)` 后退出 | `kexit()` 时其父进程已是 `initproc`，`wakeup(initproc)` 通知 init |
| 父进程 `wait(&status)` 只回收 Child | `kwait()` 只回收调用进程的直接子进程 |
| Grandchild 被 init 回收 | `initproc` 在 `main.c` 中循环调用 `wait(0)`，回收所有孤儿进程 |

---

### 6. fork + exec —— `demo_exec()`

```c
void demo_exec(void)
{
  printf("\n=== 6. fork + exec demo ===\n");
  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    return;
  }
  if (pid == 0) {
    printf("  [Child ] PID=%d, about to exec('echo')\n", getpid());
    char *args[] = {"echo", "Hello from exec!", 0};
    exec("echo", args);
    printf("  [Child ] exec failed!\n");
    exit(1);
  } else {
    printf("  [Parent] PID=%d, child PID=%d\n", getpid(), pid);
    wait(0);
    printf("  [Parent] child done\n");
  }
}
```

| 代码行 | 验证的 xv6 内核机制 |
|--------|-------------------|
| `fork()` 后父子分别执行 | 验证 `fork()` 返回两次，父子进程共享代码段但各有独立地址空间 |
| 子进程调用 `exec("echo", args)` | `kexec()` 释放旧用户页表，为 ELF 加载新页表，重置 `trapframe` 的入口点 |
| `exec` 失败后 `exit(1)` | 若 `exec` 错误返回，子进程继续执行后续代码并退出，父进程正常回收 |
| 父进程 `wait(0)` | 等待子进程执行 `echo` 完毕后回收 |

**预期输出**：子进程打印 `Hello from exec!`（由 echo 程序输出），父进程随后打印 `child done`。验证了"fork 创建进程 + exec 替换地址空间"的标准 Unix shell 模式。

---

### 汇总

| 测试 | 功能 | 验证要点 |
|------|------|---------|
| 1. 基本 fork/wait | fork 创建子进程，`wait` 回收，检查 exit 状态码 | `a0=0` 机制、`xstate` 传递 |
| 2. 父子并发 | fork 后父子交替 `pause(20)` 打印输出 | 轮询调度、`sleep/wakeup` 让出 CPU |
| 3. 多个子进程 | fork 4 次，循环 `wait` 回收 | 多子进程管理、`wait` 返回 -1 终止 |
| 4. 僵尸进程 | 子进程先 exit，父进程 delay 100 ticks 后回收 | ZOMBIE 状态、`freeproc` 回收 |
| 5. 孤儿进程 | Child fork Grandchild 后退出，Grandchild 变孤儿 | `reparent()` 托付给 `initproc` |
| 6. fork + exec | fork 后子进程 `exec("echo")` 替换为 echo 程序 | 地址空间替换、shell 基础模式 |

---

## 五、总结

xv6 的进程管理系统在有限代码量内（`proc.c` 约 758 行）实现了完整的 Unix 进程模型：

1. **进程创建（`kfork`）**：`allocproc` 分配 PCB + `uvmcopy` 复制地址空间 + `trapframe->a0=0` 实现父子返回值差异化，是 Unix fork 语义的经典实现。

2. **进程终止（`kexit`）**：`reparent()` 防止孤儿 → `ZOMBIE` 僵尸态 → 父进程 `freeproc` 回收，体现"先死后葬"的 Unix 哲学。

3. **进程回收（`kwait`）**：遍历进程表查找 ZOMBIE 子进程，`sleep/wakeup` 实现阻塞等待，`copyout` 传递退出状态。

4. **进程调度（`scheduler`）**：简单轮询 + `swtch` 上下文切换 + `wfi` 空闲等待，锁分层严谨（`wait_lock` → `p->lock` → `tickslock`）。

5. **同步机制（`sleep/wakeup`）**：基于通道地址的睡眠/唤醒，通过锁协作保证不丢失唤醒信号。

整体上 xv6 进程管理设计简洁、数据结构清晰，是学习操作系统进程管理的优秀范例。
