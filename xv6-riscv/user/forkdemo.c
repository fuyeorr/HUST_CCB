#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

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
      pause(20);
    }
    exit(0);
  } else {
    for (int i = 0; i < 3; i++) {
      printf("  [Parent] tick %d\n", i);
      pause(20);
    }
    wait(0);
  }
}

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

int main(int argc, char *argv[])
{
  if (argc <= 1) {
    printf("Usage: forkdemo <test_number>\n");
    printf("  1: basic fork/wait\n");
    printf("  2: concurrent parent/child\n");
    printf("  3: multiple children\n");
    printf("  4: zombie process\n");
    printf("  5: orphan process\n");
    printf("  6: fork + exec\n");
    printf("  all: run all tests\n");
    exit(0);
  }

  if (argv[1][0] == '1' || argv[1][0] == 'a') demo_fork();
  if (argv[1][0] == '2' || argv[1][0] == 'a') demo_concurrent();
  if (argv[1][0] == '3' || argv[1][0] == 'a') demo_multi_children();
  if (argv[1][0] == '4' || argv[1][0] == 'a') demo_zombie();
  if (argv[1][0] == '5' || argv[1][0] == 'a') demo_orphan();
  if (argv[1][0] == '6' || argv[1][0] == 'a') demo_exec();

  exit(0);
}
