#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path) {
    static char buf[DIRSIZ + 1];
    char *p;

    // Find first character after last slash.
    for (p = path + strlen(path); p >= path && *p != '/'; p--);
    p++;

    memmove(buf, p, strlen(p)+1);
    return buf;
}


void find(char *path, char *filename){
    char buf[512], *p;
    int fd;
    struct dirent de;       //目录项
    struct stat st;         //文件的统计信息

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
        case T_FILE:
            if(strcmp(fmtname(path), filename) == 0){
                printf("%s\n", path);
            }
            break;

        case T_DIR:
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {       //检查缓存是否溢出
                printf("find: path too long\n");
                break;
            }
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';
            while (read(fd, &de, sizeof(de)) == sizeof(de)) {       //read到最后一个目录项的下一次read才会返回0
                if (de.inum == 0) continue;                         //文件夹里无文件
                memmove(p, de.name, DIRSIZ);
                p[DIRSIZ] = 0;
                if(strcmp(de.name,".") == 0 || strcmp(de.name,"..") == 0){      //跳过“.”和“..”的递归
                    continue;
                }
                find(buf, filename);    //递归调用
            }
            break;
    }
    close(fd);
}

int main(int argc, char *argv[]) {
  
    if (argc == 3) {
        find(argv[1], argv[2]);
        exit(0);
    } else {
        printf("Wrong number of parameters!\n");
        exit(-1);
    }
}
