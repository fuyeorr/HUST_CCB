#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
int main()
{
    int fd;
    //1.创建shappy文件并写入内容
    printf("1. Creating file and writing content....\n");
    fd = open("shappy", O_CREATE | O_RDWR);
    if(fd < 0){
        printf("create error\n");
        exit(1);
    }
    printf("create shappy success\n");
    write(fd, "Hello,Shappy\n", 13);
    close(fd);
    //2.读取shappy文件内容
    printf("2. Reading file content...\n");
    fd = open("shappy", O_RDONLY);
    if(fd < 0){
        printf("open error\n");
        exit(1);
    }
    char buf[100];
    int n = read(fd, buf, 100);
    if(n < 0){
        printf("read error\n");
        exit(1);
    }
    printf("read file content: %s\n", buf);
    close(fd);
    //3.通过硬连接复制shappy文件并验证输出是否为hello,shappy
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
    if(n2 < 0){
        printf("read error\n");
        exit(1);
    }
    printf("read hard link content: %s\n", buf2);
    close(fd);
    //4.删除掉shappy文件并验证shappy_link文件内容是否还是hello,shappy
    printf("4. Deleting file and verifying link content...\n");
    unlink("shappy");
    fd = open("shappy_link", O_RDONLY);
    char buf3[100];
    int n3 = read(fd, buf3, 100);
    if(n3 < 0){
        printf("read error\n");
        exit(1);
    }
    printf("read link content after deletion: %s\n", buf3);
    close(fd);
    return 0;
}