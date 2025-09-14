#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 递归查找函数
void find(char *path, char *target) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    // 打开路径
    if((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // 获取文件状态
    if(fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    // 检查当前路径是否匹配目标
    // 提取路径的最后部分（文件名或目录名）
    p = path + strlen(path);
    while(p >= path && *p != '/') p--;
    p++;
    
    // 比较文件名/目录名
    if(strcmp(p, target) == 0) {
        printf("%s\n", path); // 输出相对路径
    }

    // 如果是目录，递归处理其内容
    if(st.type == T_DIR) {
        // 确保路径长度安全
        if(strlen(path) + DIRSIZ + 2 > sizeof buf) {
            fprintf(2, "find: path too long\n");
            close(fd);
            return;
        }
        
        // 添加路径分隔符
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        
        // 遍历目录项
        while(read(fd, &de, sizeof(de)) == sizeof(de)) {
            if(de.inum == 0) // 跳过空闲条目
                continue;
            
            // 跳过 "." 和 ".."
            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            
            // 构建子路径
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0; // 确保字符串结束
            
            // 递归处理子项
            find(buf, target);
        }
    }
    
    close(fd);
}

int main(int argc, char *argv[]) {
    // 参数检查
    if(argc != 3) {
        fprintf(2, "Usage: find <path> <name>\n");
        exit(1);
    }
    
    // 开始查找
    find(argv[1], argv[2]);
    exit(0);
}