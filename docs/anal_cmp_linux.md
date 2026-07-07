# xv6 与 Linux 内核特性对比分析

> 本文档基于 xv6-riscv 源码（`xv6-riscv/kernel/`）与 Linux 内核进行详尽对比。
> xv6 代码位置以注释标注，如 `// proc.c 42` 表示 `xv6-riscv/kernel/proc.c` 第 42 行。

---

## 目录

1. [总体架构对比](#1-总体架构对比)
2. [进程管理](#2-进程管理)
3. [内存管理](#3-内存管理)
4. [系统调用](#4-系统调用)
5. [中断与陷阱](#5-中断与陷阱)
6. [文件系统](#6-文件系统)
7. [同步与并发](#7-同步与并发)
8. [进程间通信(IPC)](#8-进程间通信ipc)
9. [驱动与设备模型](#9-驱动与设备模型)
10. [启动流程](#10-启动流程)
11. [总结对照表](#11-总结对照表)

---

## 1. 总体架构对比

### 1.1 设计哲学

| 维度 | xv6 | Linux |
|------|-----|-------|
| **目标** | MIT 教学操作系统，展示 OS 核心原理 | 生产级通用操作系统内核 |
| **代码规模** | ~9000 行（含注释） | ~3000 万行 |
| **架构支持** | 仅 RISC-V 64-bit (qemu virt) | x86, ARM, RISC-V, PowerPC, MIPS, s390 等 30+ 架构 |
| **许可证** | MIT | GPL-2.0 |
| **内核类型** | 单体内核 (monolithic) | 单体内核 + 可加载模块 |
| **CPU 支持** | 最多 8 核 (NCPU=8) | 数千核（NUMA 感知） |
| **抢占模型** | 协作式（主动 yield），非抢占 | 完全抢占式 (CONFIG_PREEMPT) |
| **地址空间** | Sv39 三级页表, 39-bit VA, 128MB RAM | 多级页表 (4/5-level), 支持 TB 级物理内存 |

### 1.2 内核结构

**xv6** 内核是一个扁平的 C 文件集合，所有源文件直接在 `kernel/` 目录下，无分层子目录：

```
xv6-riscv/kernel/
├── main.c          # 启动入口，依次初始化各子系统    // main.c 10-46
├── proc.c/h        # 进程管理: 调度、sleep/wakeup、fork/exit/wait
├── vm.c/h          # 虚拟内存: 页表操作、copyin/copyout
├── kalloc.c        # 物理页分配器 (freelist)
├── syscall.c/h     # 系统调用分发
├── sysproc.c       # 进程相关系统调用 (sbrk/fork/exit/wait/kill)
├── sysfile.c       # 文件系统调用 (open/read/write/close/pipe)
├── trap.c          # 中断/异常/系统调用入口 (usertrap/kerneltrap)
├── spinlock.c/h    # 自旋锁
├── sleeplock.c/h   # 睡眠锁
├── semaphore.c/h   # 信号量 (自定义扩展)
├── fs.c/h          # 文件系统核心 (inode, 目录, 路径解析)
├── file.c/h        # 文件描述符层
├── pipe.c          # 管道实现
├── exec.c          # exec() 系统调用实现
├── bio.c/h         # 块缓冲层 (buffer cache)
├── log.c           # 日志层 (crash recovery)
├── virtio_disk.c   # 磁盘驱动
├── plic.c          # PLIC 中断控制器
├── uart.c          # UART 串口驱动
├── console.c       # 控制台
├── entry.S         # 启动汇编
├── swtch.S         # 上下文切换汇编
├── trampoline.S    # 用户态/内核态切换
├── kernelvec.S     # 内核态 trap 入口
├── string.c        # 字符串工具函数
├── printk.c        # 内核 print
└── start.c         # 机器态初始化
```

**Linux** 内核具有深度分层的目录树：

```
linux/
├── arch/           # 架构相关代码 (每个架构一个子目录)
├── block/          # 块 I/O 层
├── crypto/         # 加密 API
├── drivers/        # 设备驱动 (按类型分子目录)
├── fs/             # 虚拟文件系统 (VFS) + 各文件系统实现
├── include/        # 头文件
├── init/           # 启动初始化
├── ipc/            # System V IPC
├── kernel/         # 核心: 调度、信号、时间、cgroup
├── mm/             # 内存管理
├── net/            # 网络协议栈
├── security/       # LSM 安全模块
├── sound/          # 音频
├── tools/          # 构建工具
└── virt/           # 虚拟化
```

---

## 2. 进程管理

### 2.1 进程数据结构

**xv6** 的进程结构非常精简 (`proc.h 83-105`)：

```c
// proc.h 83-105
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;       // Process state                     // proc.h 87
  void *chan;                 // If non-zero, sleeping on chan     // proc.h 88
  int killed;                 // If non-zero, have been killed     // proc.h 89
  int xstate;                 // Exit status for parent's wait     // proc.h 90
  int pid;                    // Process ID                        // proc.h 91

  struct proc *parent;        // Parent process                    // proc.h 94

  // private to process, p->lock need not be held
  uint64 kstack;              // Virtual address of kernel stack   // proc.h 97
  uint64 sz;                  // Size of process memory (bytes)     // proc.h 98
  pagetable_t pagetable;      // User page table                   // proc.h 99
  struct trapframe *trapframe; // data page for trampoline.S       // proc.h 100
  struct context context;     // swtch() here to run process       // proc.h 101
  struct file *ofile[NOFILE]; // Open files                        // proc.h 102
  struct inode *cwd;          // Current directory                 // proc.h 103
  char name[16];              // Process name (debugging)          // proc.h 104
};
```

**Linux** 使用 `task_struct`（定义于 `include/linux/sched.h`），结构体超过 100 个字段，包含：
- 多个链表节点（运行队列、等待队列、子进程链表等）
- 内存描述符 `mm_struct`
- 信号描述符 `signal_struct`
- 文件描述符表 `files_struct`
- 调度实体 `sched_entity`（包含 vruntime、权重等 CFS 参数）
- 命名空间 `nsproxy`
- cgroup 关联
- 安全上下文 `cred`

```c
// Linux include/linux/sched.h (简化示意)
struct task_struct {
    struct thread_info        thread_info;
    volatile long             state;          // TASK_RUNNING, TASK_INTERRUPTIBLE, etc.
    void                     *stack;
    int                       pid;
    struct mm_struct         *mm;             // 内存描述符
    struct files_struct      *files;          // 文件描述符表
    struct signal_struct     *signal;         // 信号处理
    struct sched_entity       se;             // CFS 调度实体
    struct list_head          children;       // 子进程链表
    struct list_head          sibling;        // 兄弟进程链表
    struct task_struct       *parent;
    struct nsproxy           *nsproxy;        // 命名空间
    struct fs_struct         *fs;             // 文件系统信息 (root/pwd)
    struct cred              *cred;           // 安全凭证 (uid, gid, capabilities)
    // ... 100+ 更多字段
};
```

### 2.2 进程状态

| xv6 (proc.h 80) | Linux | 说明 |
|-----------------|-------|------|
| `UNUSED` | - | xv6 的槽位空闲状态 |
| `USED` | `TASK_NEW` 近似 | 已分配但未就绪 |
| `SLEEPING` | `TASK_INTERRUPTIBLE` / `TASK_UNINTERRUPTIBLE` | 等待事件 |
| `RUNNABLE` | `TASK_RUNNING` (在就绪队列中) | 可运行 |
| `RUNNING` | `TASK_RUNNING` (正在 CPU 上) | 正在执行 |
| `ZOMBIE` | `EXIT_ZOMBIE` | 已退出, 等待父进程 wait |
| - | `TASK_STOPPED` | Linux 独有, ptrace/SIGSTOP |
| - | `TASK_TRACED` | Linux 独有, 被调试 |
| - | `TASK_DEAD` | Linux 独有, 最终状态 |

xv6 只有 6 种状态，而 Linux 使用位掩码组合表示更细粒度的状态。

### 2.3 进程调度

**xv6** 使用简单的轮转调度 (Round-Robin) (`proc.c 474-513`)：

```c
// proc.c 474-513
void scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for (;;) {
    intr_on();
    intr_off();              // 短暂开中断避免死锁
    int found = 0;
    for (p = proc; p < &proc[NPROC]; p++) {  // proc.c 491 - 遍历进程表
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);     // proc.c 499 - 上下文切换
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if (found == 0) {
      asm volatile("wfi");   // proc.c 510 - 无进程则等待中断
    }
  }
}
```

特点：
- 每个 CPU 轮询全局 `proc[NPROC]` 表 (NPROC=64)
- 无优先级, 无时间片计算
- 利用 RISC-V `wfi` 指令在空闲时省电
- 通过 `yield()` 主动让出 CPU (`proc.c 544-551`)
- 定时器中断触发 `yield()` (`trap.c 85-86`)

```c
// proc.c 544-551
void yield(void) {
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}
```

**Linux** 使用 CFS (Completely Fair Scheduler)：

```c
// Linux kernel/sched/fair.c (简化示意)
// CFS 使用红黑树按 vruntime 排序可运行进程
// 每次调度选择 vruntime 最小的进程 (最左侧节点)
static struct sched_entity *pick_next_entity(struct cfs_rq *cfs_rq) {
    struct rb_node *left = rb_first_cached(&cfs_rq->tasks_timeline);
    return rb_entry(left, struct sched_entity, run_node);
}
// vruntime 增量 = 实际运行时间 × (NICE_0_LOAD / 进程权重)
// nice 值越低，权重越大，vruntime 增长越慢，获得更多 CPU 时间
```

CFS 特点：
- 红黑树 (O(log n)) 而非线性扫描 (O(n))
- 虚拟运行时间 (vruntime) 跟踪公平性
- 优先级通过 nice 值映射到权重
- 支持 cgroup CPU 配额
- 支持实时调度策略 (SCHED_FIFO, SCHED_RR)
- 支持 NUMA 感知负载均衡
- EEVDF (Linux 6.6+) 替代 CFS 获得更精确的调度公平性

### 2.4 上下文切换

**xv6** 上下文切换由 `swtch.S` 汇编实现，仅保存/恢复 14 个 callee-saved 寄存器 (`swtch.S 9-40`)：

```asm
// swtch.S 9-40
// void swtch(struct context *old, struct context *new);
swtch:
        sd ra, 0(a0)        // 返回地址
        sd sp, 8(a0)        // 栈指针
        sd s0, 16(a0)       // 帧指针
        sd s1, 24(a0)
        // ... s2-s11       // swtch.S 13-23
        ld ra, 0(a1)        // 加载新上下文
        // ... 加载 s0-s11
        ret                 // 跳转到新进程的 ra
```

xv6 的 `struct context` 仅包含 callee-saved 寄存器 (`proc.h 3-20`)：

```c
// proc.h 3-20
struct context {
  uint64 ra;
  uint64 sp;
  uint64 s0; uint64 s1; uint64 s2;  uint64 s3;
  uint64 s4; uint64 s5; uint64 s6;  uint64 s7;
  uint64 s8; uint64 s9; uint64 s10; uint64 s11;
};
```

**Linux** 上下文切换 (`__switch_to` 汇编宏)：
- 保存/恢复浮点寄存器 (FPU/SSE/AVX)
- 切换页表 (写入 CR3/SATP)
- 切换线程本地存储 (TLS/FS/GS)
- 处理调试寄存器
- 保存/恢复 CET (Control-flow Enforcement) 影子栈
- 通过 `switch_mm()` 处理地址空间切换和 TLB 刷新

### 2.5 fork() 实现

**xv6** 的 fork 实现 (`proc.c 309-356`)：

```c
// proc.c 309-356
int kfork(void) {
  struct proc *np;
  struct proc *p = myproc();

  if ((np = allocproc()) == 0)        // proc.c 317 - 分配进程结构
    return -1;

  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {  // proc.c 322 - 复制用户内存
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;                     // proc.c 327

  *(np->trapframe) = *(p->trapframe); // proc.c 330 - 复制 trapframe
  np->trapframe->a0 = 0;              // proc.c 333 - 子进程返回 0

  for (i = 0; i < NOFILE; i++)        // proc.c 336-338 - 复制文件描述符
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);             // proc.c 339 - 复制当前目录

  np->parent = p;                     // proc.c 348
  np->state = RUNNABLE;               // proc.c 352
  return pid;
}
```

x86 的复制涉及完整页表的逐页复制 (`vm.c 298-326`)：

```c
// vm.c 298-326
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE) {
    if ((pte = walk(old, i, 0)) == 0)
      continue;
    if ((*pte & PTE_V) == 0)
      continue;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)         // vm.c 313 - 分配新物理页
      goto err;
    memmove(mem, (char *)pa, PGSIZE);  // vm.c 315 - 逐字节复制
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {  // vm.c 316 - 建立映射
      kfree(mem);
      goto err;
    }
  }
  return 0;
}
```

**Linux** 的 fork 使用 CoW (Copy-on-Write)：
- 子进程共享父进程的物理页，页表项标记为只读
- 任一方写入时触发页错误，内核才真正复制该页
- 通过 `dup_task_struct()` 复制 `task_struct`
- `copy_mm()` 使用 `dup_mmap()` 建立 CoW 映射
- `copy_files()` / `copy_fs()` / `copy_signal()` 等分别复制各子系统
- 通过 `CLONE_*` 标志位精细控制哪些资源共享 (命名空间/文件表/信号/地址空间)

### 2.6 exit() 与 wait()

**xv6** (`proc.c 376-416`, `proc.c 421-465`)：

```c
// proc.c 376-416
void kexit(int status) {
  struct proc *p = myproc();
  if (p == initproc) panic("init exiting");

  // 关闭所有文件
  for (int fd = 0; fd < NOFILE; fd++) {     // proc.c 385
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }
  begin_op(); iput(p->cwd); end_op();       // proc.c 393-395

  acquire(&wait_lock);
  reparent(p);                               // proc.c 401 - 子进程过继给 init
  wakeup(p->parent);                         // proc.c 404 - 唤醒父进程
  p->xstate = status;                        // proc.c 408
  p->state = ZOMBIE;                         // proc.c 409
  release(&wait_lock);

  sched();                                   // proc.c 414 - 永不返回
  panic("zombie exit");
}
```

```c
// proc.c 421-465
int kwait(uint64 addr) {
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();
  acquire(&wait_lock);
  for (;;) {
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {  // proc.c 432 - 扫描进程表
      if (pp->parent == p) {
        acquire(&pp->lock);
        havekids = 1;
        if (pp->state == ZOMBIE) {               // proc.c 438
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, ...) < 0)  // proc.c 441
            return -1;
          freeproc(pp);                           // proc.c 447 - 回收资源
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }
    if (!havekids || killed(p)) return -1;        // proc.c 457
    sleep(p, &wait_lock);                          // proc.c 463
  }
}
```

**Linux** (`kernel/exit.c`):
- `do_exit()` 依次清理: 信号 → 定时器 → 文件 → 内存 → 命名空间 → cgroup
- 设置 `EXIT_ZOMBIE` 状态后调用 `do_task_dead()` 进入最终调度
- `do_wait()` / `waitid()` 支持 `WNOHANG`, `WUNTRACED` 等选项
- Linux 需要通过信号 (SIGCHLD) 通知父进程
- 支持 waitpid() 等待特定子进程
- exit 状态编码为 `(exit_code << 8) | signal_number`

### 2.7 exec()

**xv6** (`exec.c 27-151`) 加载 ELF 可执行文件：

```c
// exec.c 27-151
int kexec(char *path, char **argv) {
  // 1. 打开 ELF 文件, 读取并验证 ELF 头
  if ((ip = namei(path)) == 0) { end_op(); return -1; }  // exec.c 42
  ilock(ip);
  if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))  // exec.c 49
    goto bad;
  if (elf.magic != ELF_MAGIC) goto bad;  // exec.c 53

  // 2. 创建新的页表
  if ((pagetable = proc_pagetable(p)) == 0) goto bad;  // exec.c 56

  // 3. 加载程序段到新地址空间
  for (i = 0, off = elf.phoff; i < elf.phnum; ...) {  // exec.c 60
    uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, ...);  // exec.c 72
    loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz);  // exec.c 76
  }

  // 4. 分配用户栈并压入参数 (argc/argv)
  sz = PGROUNDUP(sz);
  uvmalloc(pagetable, sz, sz + (USERSTACK+1)*PGSIZE, PTE_W);  // exec.c 91
  uvmclear(pagetable, sz - (USERSTACK+1)*PGSIZE);  // exec.c 95 - 栈保护页

  // 5. 将 argv[] 字符串和指针数组复制到新栈
  for (argc = 0; argv[argc]; argc++) {
    copyout(pagetable, sp, argv[argc], strlen(argv[argc])+1);  // exec.c 108
    ustack[argc] = sp;
  }
  copyout(pagetable, sp, (char*)ustack, (argc+1)*sizeof(uint64));  // exec.c 119

  // 6. 切换到新的地址空间, 释放旧页表
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;       // exec.c 135
  p->sz = sz;                     // exec.c 136
  p->trapframe->epc = elf.entry;  // exec.c 137 - 设置入口点
  p->trapframe->sp = sp;          // exec.c 138 - 设置栈指针
  proc_freepagetable(oldpagetable, oldsz);  // exec.c 139
}
```

**Linux** (`fs/exec.c`):
- `do_execveat()` → `exec_binprm()` 加载
- 支持多种二进制格式 (ELF, script, a.out) 通过 `linux_binprm` 和 `linux_binfmt` 链
- ELF loader (`fs/binfmt_elf.c`): load_elf_binary()
- 辅助向量 `AT_*` 传递内核信息给用户态
- 支持解释器 (`PT_INTERP` 段, 如 ld-linux.so)
- `vma` (虚拟内存区域) 精细管理每个段
- 安全模块 (LSM) hook 检查执行权限

---

## 3. 内存管理

### 3.1 物理内存分配

**xv6** 使用最简单的空闲链表分配器 (`kalloc.c 17-82`)：

```c
// kalloc.c 17-24
struct run {
  struct run *next;
};
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
```

```c
// kalloc.c 68-82
void *kalloc(void) {
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;  // kalloc.c 76 - 从链表头取一页
  release(&kmem.lock);
  if (r) memset((char *)r, 5, PGSIZE);  // kalloc.c 80 - 填充垃圾数据调试
  return (void *)r;
}

// kalloc.c 46-63
void kfree(void *pa) {
  // 验证地址合法性                       // kalloc.c 51
  if (((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  memset(pa, 1, PGSIZE);                  // kalloc.c 55 - 填充垃圾数据
  r = (struct run *)pa;
  acquire(&kmem.lock);
  r->next = kmem.freelist;                // kalloc.c 60 - 插入链表头
  kmem.freelist = r;
  release(&kmem.lock);
}
```

- 每次仅分配一个 4KB 页
- 使用自旋锁保护整个空闲链表
- 无法处理外部碎片，不支持更大块的分配
- 仅管理 128MB (`PHYSTOP = KERNBASE + 128*1024*1024`, `memlayout.h 40`)

**Linux** 使用伙伴系统 (Buddy System) + slab/slub/slob 分配器：
- 伙伴系统: 分配 2^order 连续物理页 (order 0~10)
- `alloc_pages(GFP_*, order)` 从对应 zone 分配
- 每个 zone 维护 `free_area[order]` 空闲链表
- `kfree()`/`kmalloc()` 从 slab cache 分配小块内存
- `vmalloc()` 分配非连续物理页，映射到连续虚拟地址
- 支持 NUMA 的 per-node zone (ZONE_DMA, ZONE_DMA32, ZONE_NORMAL, ZONE_HIGHMEM)
- 内存回收机制 (kswapd, direct reclaim, OOM killer)
- 透明大页 (THP) 支持

### 3.2 虚拟内存与页表

**xv6** 使用 RISC-V Sv39 三级页表 (`vm.c 98-116`)：

```c
// vm.c 98-116 - walk() 实现三级页表遍历
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA) panic("walk");

  for (int level = 2; level > 0; level--) {  // vm.c 104 - L2→L1→L0
    pte_t *pte = &pagetable[PX(level, va)];  // vm.c 105 - 提取每级索引
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte); // vm.c 107 - 进入下级页表
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)  // vm.c 109
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;      // vm.c 112 - 创建中间页表
    }
  }
  return &pagetable[PX(0, va)];              // vm.c 115 - 返回叶子 PTE
}
```

xv6 的 Sv39 地址分解 (`vm.c 93-96`)：

```
| 38..30 (9 bits)  | 29..21 (9 bits)  | 20..12 (9 bits)  | 11..0 (12 bits) |
|    L2 index      |    L1 index      |    L0 index      |   page offset   |
```

内核页表直接映射 (`vm.c 21-53`), 使用 1:1 的虚拟地址到物理地址：

```c
// vm.c 21-53
pagetable_t kvmmake(void) {
  pagetable_t kpgtbl;
  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // 设备 MMIO 映射
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);       // vm.c 30
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);   // vm.c 33
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);      // vm.c 36

  // 内核代码和数据映射 (1:1)
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R|PTE_X);  // vm.c 39
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R|PTE_W);  // vm.c 42

  // trampoline 页映射到最高虚拟地址
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);  // vm.c 47

  // 为每个进程映射内核栈
  proc_mapstacks(kpgtbl);  // vm.c 50
  return kpgtbl;
}
```

xv6 页表 PTE 仅支持 3 个权限位 (`PTE_R`, `PTE_W`, `PTE_X`, `PTE_U`)。

**Linux** 页表管理：
- 支持 4-level (x86-64) / 5-level (x86-64 + LA57) / 3-level (ARM LPAE) 页表
- `pgd → p4d → pud → pmd → pte`
- PTE 包含更多标志位: `_PAGE_DIRTY`, `_PAGE_ACCESSED`, `_PAGE_NX`, `_PAGE_PROTNONE` 等
- mmap/munmap/mprotect 操作 `vm_area_struct` 链表/红黑树
- 缺页异常处理 (`handle_mm_fault`): 按需分页、CoW、页面换入, 支持 5 种 fault 类型
- 反向映射 (rmap) 支持高效页面回收
- 独立的内核地址空间 (KASAN 用于检测内存错误, KASLR 用于安全随机化)

### 3.3 内存布局

**xv6** (`memlayout.h 3-60`)：

```
物理地址空间:
0x00001000 - Boot ROM (qemu)
0x02000000 - CLINT
0x0C000000 - PLIC
0x10000000 - UART0
0x10001000 - VIRTIO disk
0x80000000 - entry.S → 内核代码/数据 → 空闲物理页 → PHYSTOP (128MB)

虚拟地址空间 (用户进程):
0x00000000 ─ text (代码)
            ─ data + bss
            ─ stack (1 保护页 + 1 数据页)  // USERSTACK=1, param.h 14
            ─ heap (可扩展, sbrk)
            ...
TRAPFRAME   ─ trapframe 页
TRAMPOLINE  ─ trampoline 页 (只读可执行, 与内核共享)
MAXVA       ─ 虚拟地址空间顶部

内核栈: TRAMPOLINE - (pid+1)*2*PGSIZE, 每栈之间有保护页
```

xv6 用户栈仅 1 页 (4KB) + 1 保护页，非常受限。

**Linux** 内存布局（x86-64 典型）：

```
用户空间 (0x0000000000000000 ~ 0x00007FFFFFFFFFFF):
  0x00400000  - ELF 可执行文件加载基址 (PIE 则是随机地址)
  stack       - 默认 8MB, 可增长 (RLIMIT_STACK)
  mmap_base   - 动态库加载区域
  heap (brk)  - 程序堆

内核空间 (0xFFFF800000000000 ~ ...):
  direct mapping - 1:1 映射全部物理内存 (PAGE_OFFSET)
  vmalloc area   - 非连续内存分配
  KASLR 随机化   - 安全特性
  fixmap         - 编译时固定地址映射
  modules area   - 内核模块加载区域
```

### 3.4 内核态/用户态数据拷贝

**xv6** 使用 `copyin`/`copyout` 函数进行软件级地址转换 (`vm.c 382-405`)：

```c
// vm.c 382-405 - copyin: 从用户空间复制数据到内核
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  uint64 n, va0, pa0;
  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);              // vm.c 389 - 手动查页表获取物理地址
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0)  // vm.c 391 - 触发惰性分配
        return -1;
    }
    n = PGSIZE - (srcva - va0);
    if (n > len) n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);  // vm.c 398 - 直接物理地址拷贝
    len -= n; dst += n; srcva = va0 + PGSIZE;
  }
  return 0;
}
```

特点：每次拷贝需要手动 `walk` 查页表, 遍历三级, 无 TLB 利用。

**Linux** 使用硬件支持的方案：
- 内核可以直接访问用户空间地址（通过设置 CPU 的 SMAP/SMEP）
- `copy_from_user()` / `copy_to_user()` 依赖异常表处理缺页
- x86 使用 `rep movsb` + 异常修复表
- `get_user_pages()` 固定用户页后直接 DMA
- RISC-V 等新架构倾向于使用内核直接映射 + 软件 walk

---

## 4. 系统调用

### 4.1 系统调用分发

**xv6** 使用静态函数指针表 (`syscall.c 114-161`)：

```c
// syscall.c 114-144
static uint64 (*syscalls[])(void) = {
  [SYS_fork]    sys_fork,       // sysproc.c 26-30
  [SYS_exit]    sys_exit,       // sysproc.c 11-18
  [SYS_wait]    sys_wait,       // sysproc.c 32-38
  [SYS_pipe]    sys_pipe,       // sysfile.c 477-506
  [SYS_read]    sys_read,       // sysfile.c 68-80
  [SYS_kill]    sys_kill,       // sysproc.c 90-97
  [SYS_exec]    sys_exec,       // sysfile.c 434-475
  [SYS_fstat]   sys_fstat,      // sysfile.c 110-120
  [SYS_chdir]   sys_chdir,      // sysfile.c 409-432
  [SYS_dup]     sys_dup,        // sysfile.c 54-66
  [SYS_getpid]  sys_getpid,     // sysproc.c 20-24
  [SYS_sbrk]    sys_sbrk,       // sysproc.c 40-66
  [SYS_pause]   sys_pause,      // sysproc.c 68-88
  [SYS_uptime]  sys_uptime,     // sysproc.c 101-110
  [SYS_open]    sys_open,       // sysfile.c 304-371
  [SYS_write]   sys_write,      // sysfile.c 82-95
  [SYS_mknod]   sys_mknod,      // sysfile.c 389-407
  [SYS_unlink]  sys_unlink,     // sysfile.c 188-243
  [SYS_link]    sys_link,       // sysfile.c 123-170
  [SYS_mkdir]   sys_mkdir,      // sysfile.c 373-387
  [SYS_close]   sys_close,      // sysfile.c 97-108
  [SYS_sync]    sys_sync,       // log.c 240-252
  [SYS_sem_open]   sys_sem_open,   // sysproc.c 112-120
  [SYS_sem_wait]   sys_sem_wait,   // sysproc.c 122-131
  [SYS_sem_signal] sys_sem_signal, // sysproc.c 133-142
  [SYS_sem_close]  sys_sem_close,  // sysproc.c 144-153
  [SYS_sem_gantt]  sys_sem_gantt,  // sysproc.c 155-162
};
```

```c
// syscall.c 146-161
void syscall(void) {
  int num;
  struct proc *p = myproc();
  num = p->trapframe->a7;                   // syscall.c 152 - 从 a7 获取系统调用号
  if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();     // syscall.c 156 - 调用处理函数, 返回值写入 a0
  } else {
    printk("%d %s: unknown sys call %d\n", p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

参数传递: RISC-V 使用 a0-a5 传递最多 6 个参数 (`syscall.c 34-53`)：

```c
// syscall.c 34-53 - 从 trapframe 提取参数
static uint64 argraw(int n) {
  struct proc *p = myproc();
  switch (n) {
  case 0: return p->trapframe->a0;
  case 1: return p->trapframe->a1;
  case 2: return p->trapframe->a2;
  case 3: return p->trapframe->a3;
  case 4: return p->trapframe->a4;
  case 5: return p->trapframe->a5;
  }
  panic("argraw");
}
```

xv6 共支持 27 个系统调用 (含 5 个自定义信号量扩展)。

**Linux** 系统调用机制：
- 系统调用号通过 `sys_call_table[]` 分发 (x86-64 约 450+ 个系统调用)
- 使用 `SYSCALL_DEFINEn()` 宏定义, 自动生成类型检查代码
- 参数通过寄存器传递 (x86-64: rdi, rsi, rdx, r10, r8, r9)
- 支持 ptrace 跟踪 (seccomp/BPF 过滤)
- 系统调用入口 `entry_SYSCALL_64` 汇编, 保存/恢复复杂上下文
- 每个系统调用有独立的权限检查 (`capable()`) 和审计 hook

### 4.2 系统调用入口路径

**xv6** 路径 (`trap.c 38-94`):

```c
// trap.c 38-94 - 用户态 trap 入口
uint64 usertrap(void) {
  // ... 保存上下文 ...
  if (r_scause() == 8) {          // trap.c 54 - 检查 'ecall from user'
    if (killed(p)) kexit(-1);
    p->trapframe->epc += 4;       // trap.c 62 - sepc 指向下一条指令
    intr_on();                     // trap.c 66 - 系统调用期间允许中断
    syscall();                     // trap.c 68 - 调用 syscall() 分发
  }
  // ... 检查进程是否被杀, 必要时 yield ...
  prepare_return();               // trap.c 88
  return satp;                    // trap.c 94 - 返回用户态 satp
}
```

用户态通过 `ecall` 指令进入内核 (`trampoline.S` 中的 `uservec`), 经 `usertrap()` 最终到 `syscall()`。

**Linux** 路径：
- `syscall` (x86-64) → `entry_SYSCALL_64` → `do_syscall_64` → `sys_call_table[rax]`
- 包含更复杂的保存/恢复（全部 GPRs, 部分 FPU 状态 lazy 保存）
- 支持系统调用重启 (ERESTARTSYS) 机制
- `ptregs` 结构统一参数传递

---

## 5. 中断与陷阱

### 5.1 Trap 向量

**xv6** 使用两套向量:
- `uservec` (`trampoline.S`): 用户态 trap 入口, 映射在用户和内核地址空间最高处
- `kernelvec` (`kernelvec.S`): 内核态 trap 入口

```c
// trap.c 25-29 - 设置内核态 trap 向量
void trapinithart(void) {
  w_stvec((uint64)kernelvec);
}
```

```c
// trap.c 37-94 - 根据来源区分 trap 类型
uint64 usertrap(void) {
  w_stvec((uint64)kernelvec);           // trap.c 47 - 在内核中切换到 kernelvec
  // ...
  if (r_scause() == 8) {
    syscall();                           // trap.c 68
  } else if ((which_dev = devintr()) != 0) {
    // 设备中断                           // trap.c 69
  } else if (r_scause() == 15 || r_scause() == 13) {
    // vmfault (惰性分配页面错误)         // trap.c 71-72
  }
  prepare_return();                      // trap.c 88 - 设置返回用户态
}
```

xv6 的 RISC-V scause 编码:
- 8: Environment call from U-mode (系统调用)
- 13: Load page fault
- 15: Store page fault

```c
// trap.c 187-220 - 中断分发
int devintr() {
  uint64 scause = r_scause();
  if (scause == 0x8000000000000009L) {  // trap.c 192 - 外部中断 (PLIC)
    int irq = plic_claim();             // trap.c 196
    if (irq == UART0_IRQ) uartintr();    // trap.c 199
    else if (irq == VIRTIO0_IRQ) virtio_disk_intr();  // trap.c 201
    if (irq) plic_complete(irq);        // trap.c 210
    return 1;
  } else if (scause == 0x8000000000000005L) {  // trap.c 213 - 定时器中断
    clockintr();                         // trap.c 215
    return 2;
  }
  return 0;
}
```

**Linux** 中断/异常处理：
- 每架构有自己的中断入口 (x86: `idtentry`, ARM: 异常向量表)
- 上半部/下半部分离 (top half/bottom half)
- softirq (定时器, 网络, 块设备, tasklet 等 10 种)
- workqueue 用于可延迟工作
- 中断线程化 (threadirqs)
- 独立的中断栈 (IST)
- perf events 采样, kprobes 跟踪
- `/proc/interrupts` 暴露中断统计

### 5.2 Trampoline 机制

**xv6** 使用 trampoline 页巧妙处理用户态/内核态切换 (`trampoline.S`):

- trampoline 页同时映射在用户页表和内核页表的相同虚拟地址 `TRAMPOLINE` (MAXVA - PGSIZE)
- 用户态 `ecall` → `uservec` → 切换 satp 到内核页表, 跳转到 `usertrap`
- 返回时 `prepare_return()` 设置 trapframe, `userret` 切换回用户页表

```c
// proc.h 41-78 - trapframe 保存完整寄存器上下文
struct trapframe {
  uint64 kernel_satp;   // 内核页表                 // proc.h 42
  uint64 kernel_sp;     // 内核栈顶                  // proc.h 43
  uint64 kernel_trap;   // usertrap()               // proc.h 44
  uint64 epc;           // 用户程序计数器             // proc.h 45
  uint64 kernel_hartid; // 硬件线程 ID              // proc.h 46
  // ... 32 个通用寄存器 ...
};
```

**Linux**：
- x86-64: `syscall` 指令 (兼容模式用 `sysenter`/`int 0x80`)
- CPU 硬件自动切换特权级别和栈
- `MSR_LSTAR` 寄存器指向 `entry_SYSCALL_64`
- swapgs 切换 GS 基址（内核用 per-CPU 区域）
- RISC-V: 类似 trampoline, 但 Linux 内核也使用独立的内核地址空间 (KPTI)

---

## 6. 文件系统

### 6.1 FS 分层架构

**xv6** 七层设计 (`fs.c 1-9`):
```
系统调用接口 (sysfile.c)
    ↓
文件描述符层 (file.c)             - 抽象 pipe/inode/device
    ↓
路径名解析 (fs.c namex/namei)     - 路径查找, 目录遍历
    ↓
目录层 (fs.c dirlookup/dirlink)   - 目录项操作
    ↓
inode 层 (fs.c ialloc/iget/bmap)  - inode 分配/读写/映射
    ↓
日志层 (log.c)                    - 崩溃恢复
    ↓
块缓冲层 (bio.c)                  - LRU 缓存磁盘块
    ↓
磁盘驱动 (virtio_disk.c)          - 实际 I/O
```

**Linux** VFS 架构:
```
系统调用接口
    ↓
VFS 层 (fs/open.c, fs/read_write.c)  - 统一文件操作接口
    ↓
页缓存 (mm/filemap.c)                - 基于基数树的缓存
    ↓
具体文件系统 (ext4, xfs, btrfs...)   - 每种一个模块
    ↓
通用块层 (block/)                     - bio 请求, I/O 调度
    ↓
设备驱动
```

### 6.2 文件描述符层

**xv6** (`file.c`, `file.h`):

```c
// file.h (结构体定义)
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref;                // 引用计数
  char readable;
  char writable;
  struct pipe *pipe;      // FD_PIPE 使用
  struct inode *ip;       // FD_INODE 和 FD_DEVICE 使用
  uint off;               // FD_INODE 的文件偏移
  short major;            // FD_DEVICE 的设备主号
};
```

全局文件表 (`file.c 16-20`):
```c
// file.c 16-20
struct devsw devsw[NDEV];  // 设备表 (NDEV=10)
struct {
  struct spinlock lock;
  struct file file[NFILE]; // NFILE=100, 全局最大打开文件数
} ftable;
```

每个进程的 `ofile[NOFILE]` (NOFILE=16) (`proc.h 102`):
```c
struct file *ofile[NOFILE];  // proc.h 102
```

`filewrite`/`fileread` 根据类型分发 (`file.c 106-179`):
```c
// file.c 106-130
int fileread(struct file *f, uint64 addr, int n) {
  if (f->readable == 0) return -1;
  if (f->type == FD_PIPE)
    r = piperead(f->pipe, addr, n);     // file.c 115
  else if (f->type == FD_DEVICE)
    r = devsw[f->major].read(1, addr, n);  // file.c 119
  else if (f->type == FD_INODE) {
    ilock(f->ip);
    r = readi(f->ip, 1, addr, f->off, n); // file.c 122
    f->off += r;                         // file.c 123
    iunlock(f->ip);
  }
  return r;
}
```

**Linux** 文件描述符层：
- `struct file` 包含 f_op (file_operations 虚函数表)
- `struct file_operations`: open, read, write, llseek, mmap, poll, etc.
- 文件描述符表 `files_struct` 使用动态扩容数组 (fdtable)
- 支持 `dup`, `dup2`, `dup3`, `fcntl`
- 支持 close-on-exec 标志
- epoll 高效 I/O 多路复用 (`fs/eventpoll.c`)
- `struct path` 包含 dentry + vfsmount

### 6.3 磁盘布局与 inode

**xv6** 磁盘布局 (`fs.h 7-22`):

```
[ boot block | super block | log blocks | inode blocks | free bitmap | data blocks ]
```

```c
// fs.h 13-22 - 超级块
struct superblock {
  uint magic;        // FSMAGIC = 0x10203040
  uint size;         // 文件系统总块数
  uint nblocks;      // 数据块数
  uint ninodes;      // inode 总数
  uint nlog;         // 日志块数
  uint logstart;     // 日志起始块号
  uint inodestart;   // inode 起始块号
  uint bmapstart;    // 位图起始块号
};
```

磁盘 inode 结构 (`fs.h 31-38`):

```c
// fs.h 31-38
struct dinode {
  short type;                     // T_FILE, T_DIR, T_DEVICE (0=free)
  short major;                    // 主设备号
  short minor;                    // 次设备号
  short nlink;                    // 硬链接计数
  uint size;                      // 文件大小
  uint addrs[NDIRECT + 1];        // NDIRECT=12 + 1 个间接块
};
// 最大文件 = (12 + 1024/4) × 1024 = 268KB   // fs.h 26-28
```

内存 inode (`fs.c 177-181`):

```c
// fs.c 178-181 - 内存 inode 表
struct {
  struct spinlock lock;
  struct inode inode[NINODE];  // NINODE=50
} itable;
```

块映射支持单级间接寻址 (`fs.c 406-445`):

```c
// fs.c 406-445 - bmap(): 逻辑块号 → 物理块号
static uint bmap(struct inode *ip, uint bn) {
  if (bn < NDIRECT) {
    if ((addr = ip->addrs[bn]) == 0) {
      addr = balloc(ip->dev);            // fs.c 414 - 分配直接块
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;
  if (bn < NINDIRECT) {
    // 间接块
    if ((addr = ip->addrs[NDIRECT]) == 0) {
      addr = balloc(ip->dev);            // fs.c 426 - 分配间接块
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[bn]) == 0) {
      addr = balloc(ip->dev);            // fs.c 434 - 分配数据块
      a[bn] = addr;
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }
  panic("bmap: out of range");           // fs.c 444 - 不支持更大文件
}
```

块大小: BSIZE=1024 (`fs.h 5`), 最大文件仅 268KB (12 直接 + 256 间接)

**Linux** 文件系统：
- ext4: 默认 4KB 块, 支持 extent tree 而非间接块
- extent 支持连续大文件的高效映射
- 最大文件大小: ext4=16TB, xfs=8EB, btrfs=16EB
- inode 包含更丰富的元数据 (atime/mtime/ctime, uid/gid, acl, xattr)
- `address_space` 管理页缓存
- 写回 (writeback/ordered/journal) 多种日志模式
- 延迟分配 (delayed allocation) 减少碎片
- 支持 inline data, 快速扩展属性

### 6.4 日志与崩溃恢复

**xv6** 使用 Write-Ahead Logging (`log.c`):

```c
// log.c 40-49 - 日志结构 (struct log + 全局变量)
struct log {
  struct spinlock lock;
  int start;
  int outstanding;     // 活跃 FS 系统调用计数
  int committing;      // 是否正在提交
  int dev;
  int ncommit;
  struct logheader lh; // 含 block[] 数组记录涉及块号
};
struct log log;
```

事务流程:

```c
// log.c 128-143 - begin_op: 开始事务
void begin_op(void) {
  acquire(&log.lock);
  while (1) {
    if (log.committing)
      sleep(&log, &log.lock);              // log.c 134 - 等待提交完成
    else if (log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGBLOCKS)
      sleep(&log, &log.lock);              // log.c 137 - 日志空间不足
    else {
      log.outstanding += 1;                // log.c 139
      release(&log.lock);
      break;
    }
  }
}

// log.c 148-178 - end_op: 结束事务
void end_op(void) {
  acquire(&log.lock);
  log.outstanding -= 1;
  if (log.outstanding == 0) {              // log.c 157 - 最后一个事务, 触发提交
    do_commit = 1;
    log.committing = 1;
  }
  release(&log.lock);
  if (do_commit) {
    commit();                               // log.c 171 - 写日志, 写头部, install
    log.committing = 0;
    wakeup(&log);                           // log.c 175
  }
}
```

commit 三步走 (`log.c 196-206`):
1. `write_log()` — 将修改块写入日志区
2. `write_head()` — 写入日志头 (真正的提交点)
3. `install_trans()` — 将日志块写入磁盘实际位置, 清除日志头

恢复 (`log.c 118-125`):
```c
// log.c 118-125
static void recover_from_log(void) {
  read_head();                    // 读日志头
  install_trans(1);              // 重放已提交的事务
  log.lh.n = 0;
  write_head();                  // 清除日志
}
```

xv6 日志实现:
- 使用物理 redo log
- 自动合并 (log absorption): 同一块多次修改只记录一次 (`log.c 227-228`)
- 事务自动分组提交 (group commit)
- LOGBLOCKS = MAXOPBLOCKS*3 = 30 (`param.h 10`)

**Linux** 文件系统日志:
- ext4 支持三种模式: journal (最安全), ordered (默认, 元数据先于数据), writeback (最快)
- JBD2 (Journaling Block Device 2) 通用日志层
- 支持撤销日志 (revoke) 和 checksum
- 事务可以由多个系统调用组成
- 检查点 (checkpoint) 机制回收日志空间

### 6.5 块缓冲层

**xv6** 使用 LRU 双向链表实现 buffer cache (`bio.c`):

```c
// bio.c 25-33 - 全局 buffer cache
struct {
  struct spinlock lock;
  struct buf buf[NBUF];          // NBUF = MAXOPBLOCKS*3 = 30
  struct buf head;               // 双向链表头, head.next 是最近使用
} bcache;
```

```c
// bio.c 57-88 - bget: LRU 回收 + 查找
static struct buf *bget(uint dev, uint blockno) {
  acquire(&bcache.lock);
  // 1. 是否已缓存? (命中)
  for (b = bcache.head.next; b != &bcache.head; b = b->next)  // bio.c 65
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  // 2. 从尾部(最不常用)回收
  for (b = bcache.head.prev; b != &bcache.head; b = b->prev)  // bio.c 76
    if (b->refcnt == 0) {
      b->dev = dev; b->blockno = blockno;  // bio.c 78-79
      b->valid = 0; b->refcnt = 1;         // bio.c 80-81
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  panic("bget: no buffers");               // bio.c 87
}
```

Buffer 回收时将未引用的移到链表头部 (`bio.c 115-136`):

```c
// bio.c 115-136 - 释放 buffer 时移到链表头 (LRU)
void brelse(struct buf *b) {
  releasesleep(&b->lock);
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->next->prev = b->prev;            // bio.c 127 - 从当前位置移除
    b->prev->next = b->next;
    b->next = bcache.head.next;         // bio.c 129 - 插入头部
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  release(&bcache.lock);
}
```

特点: 仅 30 个 buffer, 单向扫描 LRU, 无 dirty/writeback 状态跟踪。

**Linux** 块层:
- 页缓存 (`address_space`) 替代 buffer cache (Linux 2.4+)
- `struct page` 和 `folio` 直接管理内存页
- 基数树/`xarray` 索引, O(1) 查找
- 支持预读 (read-ahead) 策略
- 回写线程 (`pdflush`/`bdi-flush`) 将脏页定期写回
- I/O 调度器 (mq-deadline, kyber, BFQ)
- 支持直接 I/O (O_DIRECT) 和异步 I/O (AIO/io_uring)

### 6.6 管道

**xv6** (`pipe.c`):

```c
// pipe.c 11-20
#define PIPESIZE 512

struct pipe {
  struct spinlock lock;
  char data[PIPESIZE];      // 环形缓冲区, 512 字节
  uint nread;               // 已读字节总数
  uint nwrite;              // 已写字节总数
  int readopen;             // 读端是否打开
  int writeopen;            // 写端是否打开
};
```

读/写操作使用 sleep/wakeup 同步:

```c
// pipe.c 105-134 - piperead
int piperead(struct pipe *pi, uint64 addr, int n) {
  acquire(&pi->lock);
  while (pi->nread == pi->nwrite && pi->writeopen) {  // pipe.c 113
    if (killed(pr)) { release(&pi->lock); return -1; }
    sleep(&pi->nread, &pi->lock);                   // pipe.c 118
  }
  for (i = 0; i < n; i++) {
    if (pi->nread == pi->nwrite) break;
    ch = pi->data[pi->nread % PIPESIZE];             // pipe.c 123 - 环形索引
    copyout(pr->pagetable, addr + i, &ch, 1);        // pipe.c 124
    pi->nread++;
  }
  wakeup(&pi->nwrite);                               // pipe.c 131
  release(&pi->lock);
  return i;
}

// pipe.c 76-103 - pipewrite
int pipewrite(struct pipe *pi, uint64 addr, int n) {
  acquire(&pi->lock);
  while (i < n) {
    if (pi->readopen == 0 || killed(pr)) return -1;
    if (pi->nwrite == pi->nread + PIPESIZE) {        // pipe.c 88 - 管道满
      wakeup(&pi->nread);
      sleep(&pi->nwrite, &pi->lock);                 // pipe.c 90
    } else {
      copyin(pr->pagetable, &ch, addr + i, 1);       // pipe.c 93
      pi->data[pi->nwrite++ % PIPESIZE] = ch;        // pipe.c 95
      i++;
    }
  }
  wakeup(&pi->nread);
  release(&pi->lock);
  return i;
}
```

特点:
- 仅 512 字节缓冲区
- 一次仅传输 1 字节 (无批量优化)
- 使用 `%` 取模实现环形缓冲

**Linux** pipe:
- 默认 65536 字节 (16 页), 可配置
- `pipe_read`/`pipe_write` 使用页缓存
- `splice` 系统调用实现零拷贝传输
- 轮询支持 (epoll/poll/select)
- `pipe2` 支持 O_NONBLOCK, O_CLOEXEC, O_DIRECT 标志

---

## 7. 同步与并发

### 7.1 自旋锁

**xv6** (`spinlock.c`, `spinlock.h`):

```c
// spinlock.h 4-10
struct spinlock {
  uint locked;          // 锁状态
  char *name;           // 锁名称(调试)
  struct cpu *cpu;      // 持有锁的 CPU
};
```

```c
// spinlock.c 21-42 - acquire
void acquire(struct spinlock *lk) {
  push_off();                                    // spinlock.c 24 - 禁用中断
  if (holding(lk)) panic("acquire");             // spinlock.c 25 - 死锁检测

  // 使用 RISC-V 原子交换 (amoswap.w.aq)
  while (__atomic_exchange_n(&lk->locked, 1, __ATOMIC_ACQUIRE) != 0)  // spinlock.c 37
    ;  // 自旋等待

  lk->cpu = mycpu();                             // spinlock.c 41 - 记录持有者
}

// spinlock.c 45-75 - release
void release(struct spinlock *lk) {
  if (!holding(lk)) panic("release");
  lk->cpu = 0;

  // 使用带 release 语义的原子存储 (fence rw,w + sw)
  __atomic_store_n(&lk->locked, 0, __ATOMIC_RELEASE);  // spinlock.c 72
  pop_off();                                     // spinlock.c 74 - 恢复中断
}
```

关键机制: `push_off`/`pop_off` 嵌套关中断 (`spinlock.c 91-116`):
```c
// spinlock.c 91-116 - 嵌套中断禁用
void push_off(void) {
  int old = intr_get();
  intr_off();
  if (mycpu()->noff == 0)
    mycpu()->intena = old;                       // spinlock.c 101 - 保存初始中断状态
  mycpu()->noff += 1;                            // spinlock.c 102 - 嵌套计数增加
}

void pop_off(void) {
  struct cpu *c = mycpu();
  if (intr_get()) panic("pop_off - interruptible");
  c->noff -= 1;                                  // spinlock.c 113
  if (c->noff == 0 && c->intena)
    intr_on();                                   // spinlock.c 115 - 最外层恢复
}
```

**Linux** 自旋锁：
- `spin_lock()` / `spin_unlock()` 更复杂
- `spin_lock_irqsave()` 保存/恢复中断标志
- `raw_spinlock` vs `spinlock` (可抢占内核的区分)
- 支持 RT-mutex 转换 (PREEMPT_RT)
- 排队自旋锁 (`queued_spinlock`) 避免 cache line 竞争
- `ticket spinlock` (ARM) 保证公平性

### 7.2 睡眠锁

**xv6** (`sleeplock.c`):

```c
// sleeplock.h - 睡眠锁结构
struct sleeplock {
  uint locked;
  struct spinlock lk;    // 保护 sleep 状态的自旋锁
  char *name;
  int pid;               // 持有锁的进程
};
```

```c
// sleeplock.c 21-31 - acquiresleep
void acquiresleep(struct sleeplock *lk) {
  acquire(&lk->lk);                    // sleeplock.c 24 - 获取内部自旋锁
  while (lk->locked) {                 // sleeplock.c 25
    sleep(lk, &lk->lk);                // sleeplock.c 26 - 释放自旋锁, 休眠
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;             // sleeplock.c 29
  release(&lk->lk);                    // sleeplock.c 30
}
```

xv6 的文件系统广泛使用睡眠锁 (inode 锁, buffer 锁), 允许持有期间睡眠。

**Linux**:
- `mutex`：可睡眠互斥锁, 支持乐观自旋 (`mutex_lock`)
- `rwsem`：读写信号量, 适用于读多写少场景
- `completion`：一次性同步原语
- `wait_queue_head_t` + `wait_event()`/`wake_up()` 对应 xv6 的 sleep/wakeup

### 7.3 sleep/wakeup 机制

**xv6** 使用经典的 sleep/wakeup (`proc.c 592-637`):

```c
// proc.c 592-619 - sleep
void sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();
  acquire(&p->lock);                   // proc.c 604
  release(lk);                         // proc.c 605 - 释放条件锁
  p->chan = chan;                      // proc.c 608 - 记录休眠频道
  p->state = SLEEPING;                 // proc.c 609
  sched();                             // proc.c 611 - 切换到调度器
  p->chan = 0;                         // proc.c 614 - 被唤醒后清除
  release(&p->lock);
  acquire(lk);                         // proc.c 618 - 重新获取条件锁
}

// proc.c 623-637 - wakeup
void wakeup(void *chan) {
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {  // proc.c 628 - 遍历所有进程
    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan) {  // proc.c 631
        p->state = RUNNABLE;             // proc.c 632
      }
      release(&p->lock);
    }
  }
}
```

特点:
- chan 是一个任意指针, 作为等待队列标识
- sleep 原子释放条件锁并进入休眠
- wakeup 唤醒所有在 chan 上等待的进程 (thundering herd)
- O(n) 线性扫描进程表

**Linux** 使用 wait queues (`include/linux/wait.h`):

```c
// Linux wait queue (简化示意)
DECLARE_WAIT_QUEUE_HEAD(my_queue);
wait_event(my_queue, condition);       // 等待条件为真
wake_up(&my_queue);                    // 唤醒等待者
```

- 每个等待队列用链表维护等待进程
- `wake_up` 可指定唤醒一个还是全部
- 支持可中断等待 (`wait_event_interruptible`)
- `autoremove_wake_function` 自动从队列移除
- O(k) 仅遍历在该队列上等待的进程

### 7.4 信号量 (xv6 自定义扩展)

xv6 在此基础上扩展了内核信号量 (`semaphore.c`, `semaphore.h`):

```c
// semaphore.h
struct semaphore {
  struct spinlock lock;
  int count;
  int active;
  char name[16];
};
```

```c
// semaphore.c 44-73
void sem_wait(struct semaphore *sem) {
  acquire(&sem->lock);
  sem->count--;                                    // semaphore.c 48
  if (sem->count < 0) {
    sem_record_event(..., SEM_EVENT_WAIT);          // semaphore.c 51 - 记录事件
    sleep(sem, &sem->lock);                         // semaphore.c 52 - 等待
    sem_record_event(..., SEM_EVENT_ACQUIRE);       // semaphore.c 54
  } else {
    sem_record_event(..., SEM_EVENT_ACQUIRE);       // semaphore.c 57
  }
  release(&sem->lock);
}

void sem_signal(struct semaphore *sem) {
  acquire(&sem->lock);
  sem_record_event(..., SEM_EVENT_RELEASE);         // semaphore.c 67
  sem->count++;
  if (sem->count <= 0)
    wakeup_one(sem);                                // semaphore.c 71
  release(&sem->lock);
}
```

支持全局信号量表 NSEM=128 (`proc.h 1`), 带 Gantt 图可视化功能。

**Linux** 同步原语：
- `struct semaphore` (`include/linux/semaphore.h`): `down()`/`up()`, 标准计数信号量
- System V 信号量: 进程间, 支持集合操作
- POSIX 信号量: `sem_open`/`sem_wait`/`sem_post`
- Futex 用于高效用户态锁
- RCU (Read-Copy-Update) 用于读多写少场景

---

## 8. 进程间通信(IPC)

### 8.1 xv6 的 IPC 机制

xv6 的 IPC 仅有三类：

| 机制 | 实现位置 | 说明 |
|------|---------|------|
| **pipe** | `pipe.c` | 512B 环形缓冲区, 单向, 仅父子或兄弟进程间 |
| **信号量** (扩展) | `semaphore.c` + `sysproc.c 112-162` | 内核全局信号量, NSEM=128 |
| **kill/wait/exit** | `sysproc.c` + `proc.c` | 进程间信号和状态同步 |

```c
// sysproc.c 26-30 - fork: 创建子进程作为 IPC 载体
uint64 sys_fork(void) {
  return kfork();
}

// sysproc.c 90-97 - kill: 设置 killed 标志, 非真正的信号
uint64 sys_kill(void) {
  int pid;
  argint(0, &pid);
  return kkill(pid);  // proc.c 658-677: 仅设置 p->killed = 1
}
```

xv6 `kkill` 不是信号机制, 只是设置 `p->killed` 标志, 目标进程在下一个 usertrap 或 sleep 时检查该标志并退出。

```c
// proc.c 658-677
int kkill(int pid) {
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;                           // proc.c 666 - 仅设置标志
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;                   // proc.c 669 - 唤醒等待中的进程
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
```

### 8.2 Linux 的 IPC 机制

Linux 提供了丰富的 IPC 机制:

| 机制 | 说明 |
|------|------|
| **管道** (pipe) | 无名管道, 64KB 缓冲区, 支持 splice 零拷贝 |
| **FIFO** (命名管道) | 可被任意进程使用 |
| **System V IPC** | 消息队列, 信号量, 共享内存 |
| **POSIX IPC** | mq, sem, shm |
| **信号 (signal)** | 30+ 种标准信号 + 实时信号 (RT), 支持 sigaction/sigqueue |
| **Unix domain socket** | 双向, 支持数据报和流, 传递文件描述符 (SCM_RIGHTS) |
| **eventfd** | 轻量事件通知 |
| **signalfd** | 通过 fd 接收信号 |
| **timerfd** | 通过 fd 接收定时器事件 |
| **memfd** | 匿名共享内存 |
| **dbus** / **binder** | 用户态 IPC (Android 主要使用 binder) |
| **D-Bus** | 桌面环境 IPC |
| **io_uring** | 高效 I/O 提交完成环 |

---

## 9. 驱动与设备模型

### 9.1 xv6 的驱动

xv6 驱动与内核紧密耦合, 无抽象层:

```c
// file.c 16 - 设备开关表(仅支持 UART 和 DISK)
struct devsw devsw[NDEV];  // NDEV=10, param.h 6

// 设备通过主设备号区分, 直接硬编码
// 支持的设备:
//  - UART console (console.c, uart.c)
//  - virtio disk (virtio_disk.c)
//  - PLIC 中断控制器 (plic.c)
```

UART 驱动 (`uart.c`): 轮询 + 中断驱动模式, 分别用于内核早期打印和中断模式控制台读写。

磁盘驱动 (`virtio_disk.c`): 通过 MMIO 与 qemu 的 virtio-blk 设备交互, 所有磁盘 I/O 最终调用 `virtio_disk_rw()`。

```c
// bio.c 91-103 - bread 通过 virtio_disk_rw 读取
struct buf *bread(uint dev, uint blockno) {
  struct buf *b;
  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);  // bio.c 98 - 同步读取
    b->valid = 1;
  }
  return b;
}
```

所有磁盘 I/O 都是同步的。

**Linux** 驱动模型:
- 统一设备模型 (`/sys/devices`)
- 总线 (bus)、设备 (device)、驱动 (driver) 三层模型
- 驱动自动匹配和热插拔 (udev)
- `struct device_driver` 提供 probe/remove/suspend/resume 接口
- 字符设备 (`cdev`)、块设备 (`gendisk`)、网络设备 (`net_device`)
- `ioctl` 用于设备特定控制
- 支持 `mmap` 映射设备内存到用户空间
- DMA API (`dma_alloc_coherent` 等) 隔离架构细节
- devicetree/ACPI 硬件描述

---

## 10. 启动流程

### 10.1 xv6 启动

```asm
// entry.S 1-21
// QEMU 将内核加载到 0x80000000
// 每个 hart 跳转到 _entry
_entry:
    la sp, stack0              # entry.S 12 - 设置栈 (每 CPU 4KB)
    li a0, 1024*4
    csrr a1, mhartid           # entry.S 14 - 获取 hart ID
    addi a1, a1, 1
    mul a0, a0, a1
    add sp, sp, a0             # entry.S 17 - sp = stack0 + (hartid+1)*4096
    call start                 # entry.S 19 - 跳转到 start.c
```

`start.c`: 机器态 → 管理态切换, 设置定时器中断, 跳转到 `main()`。

`main()` (`main.c 10-46`) 初始化序列:

```c
// main.c 10-46
void main() {
  if (cpuid() == 0) {            // main.c 13 - 仅 hart 0 做初始化
    consoleinit();               // main.c 14
    printkinit();                 // main.c 15
    kinit();                     // main.c 19 - 物理页分配器
    kvminit();                   // main.c 20 - 内核页表
    kvminithart();               // main.c 21 - 开启分页
    procinit();                  // main.c 22 - 进程表
    trapinit();                  // main.c 23
    trapinithart();              // main.c 24
    plicinit();                  // main.c 25
    plicinithart();              // main.c 26
    binit();                     // main.c 27 - 块缓冲
    iinit();                     // main.c 28 - inode 表
    fileinit();                  // main.c 29 - 文件表
    virtio_disk_init();          // main.c 30 - 磁盘
    sem_table_init();            // main.c 31 - 信号量表
    userinit();                  // main.c 32 - 创建第一个用户进程 init
    started = 1;                 // main.c 34
  } else {
    while (started == 0) ;       // main.c 36 - 其他 hart 等待
    kvminithart();               // main.c 40
    trapinithart();              // main.c 41
    plicinithart();              // main.c 42
  }
  scheduler();                   // main.c 45 - 所有 hart 进入调度循环
}
```

xv6 启动约 20 步即可完成, 全部位于一个文件。

**Linux** 启动流程:
- 多阶段启动 (bootloader → kernel → initramfs → init/systemd)
- 架构相关初始化 (`start_kernel()` → `arch_call_rest_init()`)
- 复杂的子系统初始化: tick, RCU, timers, workqueues, cgroup, 文件系统...
- `rest_init()` 创建 init 进程 (pid 1) 和 kthreadd (pid 2)
- 内核模块 (`*.ko`) 可以后续动态加载

---

## 11. 总结对照表

| 特性 | xv6 | Linux |
|------|-----|-------|
| **代码行数** | ~9000 | ~30M |
| **最大进程数** | 64 (NPROC) | 约 4M (PID_MAX_LIMIT) |
| **每进程打开文件数** | 16 (NOFILE) | 默认 1024, 可调至 1M+ |
| **全局打开文件数** | 100 (NFILE) | 无硬编码上限 |
| **内存 inode** | 50 (NINODE) | 动态, 受内存限制 |
| **最大 CPU** | 8 (NCPU) | 数千 (NR_CPUS 可配到 8192) |
| **RAM 支持** | 128MB | TB 至 PB 级 |
| **最大文件大小** | 268KB (12 直接 + 256 间接, 1KB 块) | 16TB (ext4) ~ 8EB (xfs) |
| **文件系统** | 自定义 (简化 inode-based) | ext4, xfs, btrfs, f2fs, nfs... 60+ |
| **网络支持** | 无 | 完整 TCP/IP 协议栈 |
| **调度器** | 简单轮转 (Round-Robin) | CFS/EEVDF + RT |
| **同步原语** | 自旋锁, 睡眠锁, sleep/wakeup | 自旋锁, mutex, rwsem, RCU, futex, completion... |
| **IPC** | pipe, kill/exit | pipe, FIFO, socket, signal, SysV/POSIX IPC, eventfd... |
| **内存分配** | 空闲链表 (单页) | 伙伴系统 + slab/slub |
| **虚拟内存** | 三级页表, 直接映射 | 多级页表, 按需分页, 交换, mmap |
| **中断处理** | 统一入口 + 设备分发 | 上半部/下半部, softirq, tasklet, workqueue |
| **与架构无关性** | 低 (RISC-V only) | 高 (30+ 架构) |
| **内核抢占** | 无 (协作式) | 完全抢占 (CONFIG_PREEMPT) |
| **内核线程** | 无 | 有 (kthread, workqueue) |
| **用户/内核隔离** | ecall + trampoline | 架构特定 (syscall/int 0x80/svc) |
| **安全** | 无 (root 仅用作路径) | LSM, SELinux, AppArmor, capabilities, seccomp |
| **模块化** | 无, 全部静态链接 | 可加载模块 (`*.ko`) |
| **构建系统** | 手写 Makefile | KBuild 系统 |
| **文档** | 注释 + xv6 book | 内核文档 + 手册页 + 书籍 |
| **调试** | printk + QEMU gdb | kgdb, ftrace, perf, kprobes, eBPF, printk 分级 |

### 核心区别总结

1. **规模与目标**: xv6 是教学系统, 专注概念清晰; Linux 是生产系统, 追求性能和可扩展性。

2. **内存管理**: xv6 仅支持按需分页惰性分配, 无 swap/CoW/KSM/huge page; Linux 拥有完整的虚拟内存管理。

3. **进程管理**: xv6 使用 O(n) 轮询调度 + 简单 fork (完全复制地址空间); Linux 使用 O(log n) CFS + CoW fork。

4. **文件系统**: xv6 最大文件 268KB, 单级间接寻址; Linux 支持 extent tree, 文件可达 TB/EB 级。

5. **并发**: xv6 在整个内核中使用少量自旋锁 + 睡眠锁; Linux 采用细粒度锁 + RCU + per-CPU 数据结构。

6. **设备驱动**: xv6 仅有 UART 和 virtio disk 驱动; Linux 拥有数千种设备驱动, 统一驱动模型。

7. **网络**: xv6 完全没有网络支持; Linux 拥有完整的 TCP/IP 协议栈和网络设备驱动。

8. **安全**: xv6 无任何安全机制; Linux 有 LSM, capabilities, seccomp, namespace/cgroup 隔离, KASLR 等多层防护。

9. **信号量扩展**: 当前 xv6 代码库扩展了内核信号量机制 (`semaphore.c`), 支持 sem_open/wait/signal/close 系统调用和 Gantt 图可视化, 这是标准 xv6 之外的增强功能。
