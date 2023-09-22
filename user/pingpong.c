#include "kernel/types.h"
#include "user.h"

#define MAXSIZE 512

int main(int argc, char* argv[]){
    
    int pipefd1[2];   //管道1传递ping:父进程->子进程
    int pipefd2[2];   //管道2传递pong:子进程->父进程

    char buffer[MAXSIZE];
    
    //判断管道是否出现错误
    if (pipe(pipefd1) < 0 || pipe(pipefd2) < 0) {
        printf("Pipe Error!\n");
        exit(-1);
    }

    /* 正常创建后，p[1]为管道写入端，p[0]为管道读出端*/
    if (fork() == 0) { 
        /* 子进程 */
        /* 子进程读管道，父进程写管道 */
        close(pipefd1[1]); // 关闭写端
        read(pipefd1[0], buffer, MAXSIZE);
        printf("%d: received %s\n", getpid(), buffer);        
        close(pipefd1[0]); // 读取完成，关闭读端

        /* 子进程写管道，父进程读管道 */
        close(pipefd2[0]); // 关闭读端
        write(pipefd2[1], "pong", MAXSIZE);       
        close(pipefd2[1]); // 写入完成，关闭写端

    } else if (fork()>0) { 
        /* 父进程 */
        /* 子进程读管道，父进程写管道 */
        close(pipefd1[0]); // 关闭读端
        write(pipefd1[1], "ping", MAXSIZE);      
        close(pipefd1[1]); // 写入完成，关闭写端

        /* 子进程写管道，父进程读管道 */
        close(pipefd2[1]); // 关闭写端
        read(pipefd2[0], buffer, MAXSIZE);
        printf("%d: received %s\n", getpid(), buffer);        
        close(pipefd2[0]); // 读取完成，关闭读端
    }

    exit(0);
}