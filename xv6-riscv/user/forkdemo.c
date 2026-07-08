#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int lockfd[2];

void
printlock(void)
{
  char c;
  read(lockfd[0], &c, 1);
}

void
printunlock(void)
{
  write(lockfd[1], "x", 1);
}

void demo_fork(void)
{
  int pid;

  printlock();
  printf("\n=== 1. Basic fork() demo ===\n");
  printf("Parent PID = %d, about to fork...\n", getpid());
  printunlock();

  pid = fork();
  if (pid < 0) {
    printlock();
    printf("fork failed\n");
    printunlock();
    return;
  }

  if (pid == 0) {
    printlock();
    printf("  [Child ] PID=%d, fork() returned=%d\n", getpid(), pid);
    printf("  [Child ] doing some work...\n");
    printunlock();
    exit(7);
  } else {
    printlock();
    printf("  [Parent] fork() returned child PID=%d\n", pid);
    printf("  [Parent] waiting for child...\n");
    printunlock();
    int status;
    int wpid = wait(&status);
    printlock();
    printf("  [Parent] wait() returned PID=%d, exit status=%d\n", wpid, status);
    printunlock();
  }
}

void demo_concurrent(void)
{
  printlock();
  printf("\n=== 2. Concurrent parent/child demo ===\n");
  printunlock();
  int pid = fork();
  if (pid < 0) {
    printlock();
    printf("fork failed\n");
    printunlock();
    return;
  }
  if (pid == 0) {
    for (int i = 0; i < 3; i++) {
      printlock();
      printf("  [Child ] tick %d\n", i);
      printunlock();
      pause(20);
    }
    exit(0);
  } else {
    for (int i = 0; i < 3; i++) {
      printlock();
      printf("  [Parent] tick %d\n", i);
      printunlock();
      pause(20);
    }
    wait(0);
  }
}

void demo_multi_children(void)
{
  printlock();
  printf("\n=== 3. Multiple children demo ===\n");
  printunlock();
  int i, pid;

  for (i = 0; i < 4; i++) {
    pid = fork();
    if (pid < 0) {
      printlock();
      printf("  fork failed at i=%d\n", i);
      printunlock();
      break;
    }
    if (pid == 0) {
      printlock();
      printf("  [Child #%d] PID=%d started\n", i, getpid());
      printunlock();
      pause(30 * (i + 1));
      printlock();
      printf("  [Child #%d] PID=%d exiting\n", i, getpid());
      printunlock();
      exit(i);
    }
    printlock();
    printf("  [Parent  ] spawned child #%d PID=%d\n", i, pid);
    printunlock();
  }

  printlock();
  printf("  [Parent  ] waiting for all children...\n");
  printunlock();
  int status;
  while ((pid = wait(&status)) > 0) {
    printlock();
    printf("  [Parent  ] child PID=%d exited with status=%d\n", pid, status);
    printunlock();
  }
  printlock();
  printf("  [Parent  ] no more children, wait()=%d\n", pid);
  printunlock();
}

void demo_zombie(void)
{
  printlock();
  printf("\n=== 4. Zombie process demo ===\n");
  printunlock();
  int pid = fork();
  if (pid < 0) {
    printlock();
    printf("fork failed\n");
    printunlock();
    return;
  }
  if (pid == 0) {
    printlock();
    printf("  [Child ] PID=%d, exiting immediately\n", getpid());
    printunlock();
    exit(0);
  } else {
    printlock();
    printf("  [Parent] PID=%d, child PID=%d\n", getpid(), pid);
    printf("  [Parent] sleeping 100 ticks (child is zombie)...\n");
    printunlock();
    pause(100);
    printlock();
    printf("  [Parent] now calling wait() to reap zombie\n");
    printunlock();
    wait(0);
    printlock();
    printf("  [Parent] zombie reaped\n");
    printunlock();
  }
}

void demo_orphan(void)
{
  printlock();
  printf("\n=== 5. Orphan process demo ===\n");
  printunlock();
  int pid = fork();
  if (pid < 0) {
    printlock();
    printf("fork failed\n");
    printunlock();
    return;
  }
  if (pid == 0) {
    int pid2 = fork();
    if (pid2 < 0) {
      printlock();
      printf("  fork2 failed\n");
      printunlock();
      exit(1);
    }
    if (pid2 == 0) {
      printlock();
      printf("  [Grandchild] PID=%d, I am an orphan now\n", getpid());
      printf("  [Grandchild] sleeping 50 ticks...\n");
      printunlock();
      pause(50);
      printlock();
      printf("  [Grandchild] exiting (should be adopted by init)\n");
      printunlock();
      exit(0);
    }
    printlock();
    printf("  [Child] PID=%d exiting, grandchild becomes orphan\n", getpid());
    printunlock();
    exit(0);
  } else {
    printlock();
    printf("  [Parent] PID=%d, child PID=%d\n", getpid(), pid);
    printunlock();
    int status;
    wait(&status);
    printlock();
    printf("  [Parent] child exited, status=%d\n", status);
    printf("  [Parent] grandchild was reaped by init\n");
    printunlock();
  }
}

void demo_exec(void)
{
  printlock();
  printf("\n=== 6. fork + exec demo ===\n");
  printunlock();
  int pid = fork();
  if (pid < 0) {
    printlock();
    printf("fork failed\n");
    printunlock();
    return;
  }
  if (pid == 0) {
    printlock();
    printf("  [Child ] PID=%d, about to exec('echo')\n", getpid());
    printunlock();
    char *args[] = {"echo", "Hello from exec!", 0};
    exec("echo", args);
    printlock();
    printf("  [Child ] exec failed!\n");
    printunlock();
    exit(1);
  } else {
    printlock();
    printf("  [Parent] PID=%d, child PID=%d\n", getpid(), pid);
    printunlock();
    wait(0);
    printlock();
    printf("  [Parent] child done\n");
    printunlock();
  }
}

int main(int argc, char *argv[])
{
  if (argc <= 1) {
    printlock();
    printf("Usage: forkdemo <test_number>\n");
    printf("  1: basic fork/wait\n");
    printf("  2: concurrent parent/child\n");
    printf("  3: multiple children\n");
    printf("  4: zombie process\n");
    printf("  5: orphan process\n");
    printf("  6: fork + exec\n");
    printf("  all: run all tests\n");
    printunlock();
    exit(0);
  }

  pipe(lockfd);
  write(lockfd[1], "x", 1);

  if (argv[1][0] == '1' || argv[1][0] == 'a') demo_fork();
  if (argv[1][0] == '2' || argv[1][0] == 'a') demo_concurrent();
  if (argv[1][0] == '3' || argv[1][0] == 'a') demo_multi_children();
  if (argv[1][0] == '4' || argv[1][0] == 'a') demo_zombie();
  if (argv[1][0] == '5' || argv[1][0] == 'a') demo_orphan();
  if (argv[1][0] == '6' || argv[1][0] == 'a') demo_exec();

  exit(0);
}
