# xv6 操作系统的文件系统分析

## 一、xv6 文件系统的特点

xv6 的文件系统设计简洁，但完整实现了 Unix 文件系统的核心概念。以下结合源码分析其主要特点。

### 1. 一切皆文件，用 fd 统一抽象

在 xv6 中，所有 I/O 操作都通过文件描述符进行。`open` 返回的整数 fd 被进程用于后续的 `read`、`write`、`close` 操作。

源码参考：`kernel/file.c` 中的 `filealloc`、`filedup`、`fileclose` 等函数管理 `struct file` 数组（`ftable`，共 `NFILE=100` 个槽位），每个进程通过 `proc->ofile[NOFILE=16]` 维护自己的打开文件表。

### 2. 磁盘布局：超级块 + 日志 + inode + 位图 + 数据块

xv6 的磁盘结构定义在 `kernel/fs.h` 中：

```c
// kernel/fs.h:4-5
#define ROOTINO  1   // 根目录 inode 号
#define BSIZE 1024   // 块大小 1024 字节

// kernel/fs.h:13-22 超级块结构
struct superblock {
  uint magic;        // 文件系统幻数 (FSMAGIC=0x10203040)
  uint size;         // 文件系统总块数
  uint nblocks;      // 数据块数量
  uint ninodes;      // inode 数量
  uint nlog;         // 日志块数量
  uint logstart;     // 日志起始块号
  uint inodestart;   // inode 起始块号
  uint bmapstart;    // 位图起始块号
};
```

磁盘布局（`kernel/fs.h:7-9`）：

```
// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
```

### 3. inode 是文件系统的核心

每个文件/目录都由一个 inode 唯一标识，inode 记录文件类型、大小、数据块指针等，但不包含文件名。

源码参考：`kernel/fs.h:31-38` 的 `struct dinode`（磁盘 inode）和 `kernel/file.h:17-30` 的 `struct inode`（内存 inode）。

```c
// kernel/fs.h:26-28
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))  // = 256
#define MAXFILE (NDIRECT + NINDIRECT)     // = 268 块

// kernel/fs.h:31-38
struct dinode {
  short type;            // 文件类型：T_DIR(1) / T_FILE(2) / T_DEVICE(3)
  short major;           // 主设备号
  short minor;           // 次设备号
  short nlink;           // 硬链接计数
  uint size;             // 文件大小（字节）
  uint addrs[NDIRECT+1]; // 数据块地址（12 直接 + 1 间接）
};
```

### 4. 目录是特殊的文件

目录文件的内容是一系列 `struct dirent`，每项包含 inode 号和文件名。这实现了文件名到 inode 的映射。

```c
// kernel/fs.h:53-59
#define DIRSIZ 14

struct dirent {
  ushort inum;       // inode 号（0 表示空闲）
  char name[DIRSIZ]; // 文件名（最长 14 字符，不以 NUL 结尾）
};
```

### 5. 硬链接共享同一个 inode

硬链接的实质是在某个目录下创建一个新的目录项，使其 `inum` 指向已存在的 inode。这导致 inode 的 `nlink` 字段递增。只有当 `nlink` 降为 0 **且**最后一个引用被释放时（`iput` 中 `ref==1 && nlink==0`），inode 和数据块才被真正释放。

源码参考：`kernel/sysfile.c:123-170` 中 `sys_link` 函数：

```c
// kernel/sysfile.c:138-147（先增加 nlink，再创建目录项）
ilock(ip);
if (ip->type == T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
}

ip->nlink++;       // 先增加链接计数
iupdate(ip);       // 写回磁盘
iunlock(ip);

// 然后解析父目录并添加目录项
if ((dp = nameiparent(new, name)) == 0)
    goto bad;
ilock(dp);
if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
    iunlockput(dp);
    goto bad;
}
iunlockput(dp);
```

> **注意：** `sys_link` 先 `nlink++` 再创建目录项。如果顺序相反，在创建目录项后、`nlink++` 前发生崩溃，恢复后目录项将指向一个 `nlink` 偏低的 inode，但 xv6 的日志机制保证这两步在同一事务中，不会出现中间状态。

`dirlink` 内部通过 `writei` 将新的目录项写入父目录的数据块（`kernel/fs.c` 中 `dirlink` 函数）。

### 6. unlink 删除目录项，而非直接删除文件

`unlink` 仅删除目录中的一项记录，并将对应 inode 的 `nlink` 减 1。若 `nlink` 变为 0，`iput` 会释放 inode 及其数据块。

源码参考：`kernel/sysfile.c:188-243` 中 `sys_unlink` 函数：

```c
// kernel/sysfile.c:222-233（清除目录项并减少 nlink）
memset(&de, 0, sizeof(de));
if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");

iunlockput(dp);

ip->nlink--;
iupdate(ip);
iunlockput(ip);
```

当 `iput` 检测到 `ref==1 && nlink==0` 时（`kernel/fs.c:343-355`）：

```c
// kernel/fs.c:352-355（实际释放逻辑，无 ifree 函数）
itrunc(ip);       // 释放所有数据块（直接块 + 间接块）
ip->type = 0;     // 将 inode 标记为空闲
iupdate(ip);      // 写回磁盘
ip->valid = 0;    // 标记为无效
```

> xv6 中**没有** `ifree` 函数，inode 的释放是通过将 `type` 清零并调用 `iupdate` 实现的。

### 7. 日志机制保证崩溃一致性

文件系统操作（如 `create`、`write`、`link`、`unlink`）涉及多次磁盘写，日志层保证其原子性。

`kernel/log.c` 中的提交流程：

```
begin_op()          → 开始事务
log_write(bp)       → 在内存中记录被修改的块号（非真正写入）
end_op()            → 结束事务，触发 commit
  commit():
    1. write_log()       → 将修改的数据块复制到日志区域
    2. write_head()      → 写入日志头（原子提交点）
    3. install_trans(0)  → 从日志复制到实际位置
    4. write_head()      → 清除日志头（n=0）
```

重启时 `recover_from_log()` 检测日志头 `n>0` 则重放未完成的事务。

---

## 二、filetest.c 代码功能原理分析

`user/filetest.c` 依次测试了 xv6 文件系统的基本操作：创建/写入 → 读取 → 硬链接 → 删除原文件后验证链接可用。

### 步骤 1：创建文件并写入内容

```c
// filetest.c:9-17
printf("1. Creating file and writing content...\n");
fd = open("shappy", O_CREATE | O_RDWR);
if(fd < 0){
    printf("create error\n");
    exit(1);
}
printf("create shappy success\n");
write(fd, "Hello,Shappy\n", 13);
close(fd);
```

**原理：**

- `open("shappy", O_CREATE | O_RDWR)` 调用 `sys_open`（`kernel/sysfile.c:305-371`），携带 `O_CREATE` 标志触发 `create(path, T_FILE, 0, 0)`（`kernel/sysfile.c:246-302`），执行：
  1. `nameiparent("shappy", name)` 解析当前工作目录的父目录
  2. `ialloc(dp->dev, T_FILE)` 分配新 inode，设置 `type=T_FILE`，`nlink=1`
  3. `dirlink(dp, "shappy", ip->inum)` 在当前目录写入目录项
- 全部在 `begin_op()/end_op()` 事务内完成
- `write(fd, "Hello,Shappy\n", 13)` 调用 `filewrite` → `writei`（`kernel/fs.c:530-550`），通过 `bmap` 分配数据块，写入数据后 `log_write` 记录日志
- `close(fd)` 最终调用 `iput`，此时 `nlink=1`，inode 保留

### 步骤 2：读取文件内容

```c
// filetest.c:19-32
printf("2. Reading file content...\n");
fd = open("shappy", O_RDONLY);
// ...
char buf[100];
int n = read(fd, buf, 100);
// ...
printf("read file content: %s\n", buf);
close(fd);
```

**原理：**

- `open("shappy", O_RDONLY)` 通过 `namei`（`kernel/fs.c`）在当前目录中查找 `shappy`，获取其 inode
- `read(fd, buf, 100)` 调用 `fileread` → `readi`（`kernel/fs.c:496-520`），从 inode 的数据块中读取内容到 `buf`。读操作不修改磁盘，无需日志事务

### 步骤 3：创建硬链接并验证内容

```c
// filetest.c:33-50
printf("3. Creating hard link for shappy file...\n");
int num;
num=link("shappy", "shappy_link");
if(num < 0){
    printf("link error\n");
    exit(1);
}
printf("create hard link success\n");
fd = open("shappy_link", O_RDONLY);
char buf2[100];
int n2 = read(fd, buf2, 100);
// ...
printf("read hard link content: %s\n", buf2);
close(fd);
```

**原理：**

- `link("shappy", "shappy_link")` 调用 `sys_link`（`kernel/sysfile.c:123-170`）：
  1. `namei("shappy")` 找到 `shappy` 的 inode `ip`
  2. `ilock(ip)`，`ip->nlink++`（从 1 变为 2），`iupdate(ip)` 写回磁盘
  3. `nameiparent("shappy_link", name)` 找到父目录（当前目录）
  4. `dirlink(dp, "shappy_link", ip->inum)` 写入新的目录项
- 之后打开 `shappy_link`，`namei` 解析到**同一个 inode**，因此读取的内容仍是 `"Hello,Shappy\n"`

### 步骤 4：删除原文件，验证链接文件仍可用

```c
// filetest.c:52-62
printf("4. Deleting file and verifying link content...\n");
unlink("shappy");
fd = open("shappy_link", O_RDONLY);
char buf3[100];
int n3 = read(fd, buf3, 100);
// ...
printf("read link content after deletion: %s\n", buf3);
close(fd);
```

**原理：**

- `unlink("shappy")` 调用 `sys_unlink`（`kernel/sysfile.c:188-243`）：
  1. `nameiparent("shappy", name)` 解析父目录和文件名
  2. `dirlookup(dp, name, &off)` 找到 `shappy` 的目录项
  3. `writei(dp, ...)` 将目录项清零（删除记录）
  4. `ip->nlink--`（从 2 减为 1），`iupdate(ip)` 写回磁盘
  5. `nlink=1 > 0`，所以 `iput` 不会触发释放
- 因此 `shappy_link` 仍然可以正常打开并读取到 `"Hello,Shappy\n"`
- 这正是硬链接的核心特性：**只有当 `nlink` 降为 0 且 `ref` 也归零时，文件数据才被真正删除**

---

## 三、总结

xv6 的文件系统以 inode 为中心，通过**日志层**保证多步操作的崩溃原子性，通过**硬链接 nlink 机制**实现文件名的多对一映射。`filetest.c` 的四个步骤完整验证了这些核心特性：

1. `open` + `write` 验证了 inode 分配、数据块按需分配（`bmap`/`balloc`）和日志事务
2. `read` 验证了 inode 数据读取路径（`readi`/`bread`）
3. `link` 验证了 `nlink` 递增和目录项共享 inode
4. `unlink` 验证了 `nlink` 递减和**延迟释放**语义——只要还有至少一个硬链接，文件数据就不会丢失

整体上 xv6 文件系统的设计体现了**简洁但完整**的 Unix 哲学：数据结构清晰（`dinode`/`inode`/`file`/`dirent`），分层明确（块缓存 → 日志 → inode → 目录 → 路径名 → 系统调用），在有限代码量内实现了崩溃安全和多用户并发访问。
