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

## 四、forkdemo.c 测试用例功能说明

6 个测试用例覆盖 xv6 进程管理的核心场景（源码详见 `user/forkdemo.c`）：

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
