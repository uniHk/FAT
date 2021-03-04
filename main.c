/*
 * @Author: uniHk
 * @Date: 2020-12-14 18:38:21
 * @LastEditTime: 2020-12-24 13:02:37
 */

#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fs.h"

/**
 * @description: 命令列表，紧接着是对应的函数指针列表
 */
char *builtin_str[] = {
    "format",
    "cd",
    "mkdir",
    "rmdir",
    "ls",
    "create",
    "rm",
    "write",
    "read",
    "exit",
    "open",
    "close",
    "pwd",
    "openlist"
};

int (*built_func[])(char **) = {
    &my_format,
    &my_cd,
    &my_mkdir,
    &my_rmdir,
    &my_ls,
    &my_create,
    &my_rm,
    &my_write,
    &my_read,
    &my_exit_sys,
    &my_open,
    &my_close,
    &my_pwd,
    &my_openlist
};

int csh_num_builtins(void) {
    return sizeof(builtin_str) / sizeof(char *);
}

/**
 * @description: 启动一个非自行实现的程序
 * @param {args 最后要以 NULL 结尾}
 */
int csh_launch(char **args) {
    pid_t pid, wpid;
    int state;
    
    pid = fork();
    if (pid == 0) {
        if (execvp(args[0], args) == -1) {          // 替换失败
            perror("csh");
        }
        exit(EXIT_FAILURE);
    }
    else if (pid < 0) {
        perror("csh");
    }
    else {
        do {
            wpid = waitpid(pid, &state, WUNTRACED); // 子进程被阻塞后立马返回状态
        } while (!WIFEXITED(state) && !WIFSIGNALED(state)); // exit 的值为0 && 不是异常退出
    }

    return 1;
}

/**
 * @description: 执行命令
 */
int csh_execute(char **args) {
    if (args[0] == NULL)
        return 1;
    
    for (int i = 0; i < csh_num_builtins(); ++i) {
        if (strcmp(args[0], builtin_str[i]) == 0)
            return (*built_func[i])(args);
    }

    return csh_launch(args);                        // 不是自行实现的函数
}

/**
 * @description: 命令读入
 */
char *csh_read_line(void) {
    char *line = NULL;
    size_t bufsize = 0;
    getline(&line, &bufsize, stdin);
    return line;
}

#define CSH_TOK_BUFSIZE 64                          // tokens 动态分配的单增大小
#define CSH_TOK_DELIM   " \t\r\n\a"                 // 5 种分隔符

/**
 * @description: 分割命令
 */
char **csh_split_line(char *line) {
    int bufsize = CSH_TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token = NULL;

    if (!tokens) {
        fprintf(stderr, "csh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, CSH_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {                  // 被分割出太多字符串了
            bufsize += CSH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens) {
                fprintf(stderr, "csh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, CSH_TOK_DELIM);
    }
    tokens[position] = NULL;                        // 别忘了这个
    return tokens;
}

/**
 * @description: 主循环
 */
void csh_loop(void) {
    char *line = NULL;
    char **args = NULL;
    int state;
    
    do {
        printf("\e[31;1muniHk \e[37;1m%s\e[0m ", current_dir);
        printf("\e[32m$\e[0m ");
        line = csh_read_line();
        args = csh_split_line(line);
        state = csh_execute(args);

        free(line);
        free(args);
    } while (state);
}

int main(int argc, char **argv) {
    start_sys();
    csh_loop();

    return EXIT_SUCCESS;
}