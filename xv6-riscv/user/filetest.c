#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
int main()
{
    int fd;
    fd = open("shappy.txt", O_CREATE | O_RDWR);
    if(fd < 0){
        printf("create error\n");
        exit(1);
    }
    printf("create shappy.txt success\n");
    write(fd, "shappy\n", 7);
    return 0;
}
