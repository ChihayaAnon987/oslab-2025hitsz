#include "kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]){
    if(argc != 2){
        printf("Sleep needs one argument!\n"); // 检查参数数量是否正确
        exit(-1);
    }
    int ticks = atoi(argv[1]); // 将字符串参数转为整数
    sleep(ticks); // 使用系统调用 sleep
    printf("nothing happens for a little while\n");
    exit(0); // 确保进程退出
}
// 1. 当输入 sleep hello world\n 时，argc 和 argv 的大小
// argc 的值是 3（对应 "sleep", "hello", "world" 三个字符串）。
// argv 数组的元素个数为 argc + 1，也就是 4 个指针元素（最后一个 argv[3] 为 NULL 作结束标志）。
// 如果按字节计算（xv6 为 32 位，指针 4 字节），则 argv 数组占用的内存为 (argc+1) * 4 = 16 字节。
// 
// 2. main 函数参数 argv 中的指针分别指向什么字符串及其含义
// argv[0] → 指向字符串 "sleep"，表示程序名（由 shell 填入，通常是执行的命令名）。
// argv[1] → 指向字符串 "hello"，表示第一个命令行参数。
// argv[2] → 指向字符串 "world"，表示第二个命令行参数。
// argv[3] → 为 NULL，表示参数列表结束。
// 这些字符串都是以 '\0' 结尾的 C 字符串，通常由 shell 在 exec 前拷贝到新进程的用户栈上，argv 中的指针就是指向用户栈中相应字符串的地址。
// 
// 3. 程序 sleep 使用了哪些系统调用
// sleep(...) 本身是对内核的系统调用（xv6 提供的 sleep 系统调用，用于阻塞当前进程若干时钟滴答）。
// printf(...)（用户库函数）在底层会通过 write 系统调用将输出写到标准输出（TTY）。所以 printf 会导致一次或多次 write 系统调用。
// exit(...) 调用会触发内核的 exit 系统调用，结束进程。
// 注意：atoi 是用户空间的库函数，不是系统调用。
// （另外补充：在 shell 启动该程序之前，shell 会调用 fork 和 exec，这些也都是系统调用，但它们属于 shell 的行为，不是 sleep 程序内部直接发出的系统调用。）