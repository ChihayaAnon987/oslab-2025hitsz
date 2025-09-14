#include "kernel/types.h"
#include "user.h"
 
int main(int argc,char* argv[]){
    //创建两个管道，分别实现ping、pong的读写
    int p[2];
    pipe(p);
    char readtext[10];//作为父进程和子进程的读出容器
    //子程序读出
    int parent_pid = getpid();
    int pid = fork();
    if(pid == 0) {
        read(p[0], readtext, 10);
        printf("%d: received %s from pid %d\n", getpid(), readtext, parent_pid);
        write(p[1], "pong", 10);
        exit(0);//子进程一定要退出
    }
    //父程序写入
    else{
        write(p[1], "ping", 10);
        wait(0);//父进程阻塞，等待子进程读取
        read(p[0], readtext, 10);
        printf("%d: received %s from pid %d\n", getpid(), readtext, pid);
        exit(0);//父进程一定要退出
    }
    return 0;
}