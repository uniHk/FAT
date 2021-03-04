/*
 * @Author: uniHk
 * @Date: 2020-12-14 18:38:07
 * @LastEditTime: 2020-12-17 11:50:06
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define BLOCK_SIZE      1024        // 1 << 10
#define BLOCK_NUM       1024        // 1 << 10
#define DISK_SIZE       1048576     // 1 << 20
#define SYS_PATH        "./fsfile"  // 文件系统磁盘保存
#define END             0xffff
#define FREE            0x0000
#define ROOT            "/"
#define ROOT_BLOCK_NUM  2
#define MAX_OPENFILE    10
#define NAMELENGTH      32
#define PATHLENGTH      128
#define DELIM           "/"
#define FOLDER_COLOR    "\e[1;32m"  // 加粗 + 绿色
#define DEFAULT_COLOR   "\e[0m"
#define WRITE_SIZE      20 * BLOCK_SIZE
#define hhh             printf("hhh\n")

/**
 * @description: 引导块
 */
typedef struct BLOCK0 {
    char information[200];
    unsigned short root;            // 默认为 5
    unsigned char *start_block;     // 默认为 7
} block0;

/**
 * @description: 文件控制块
 */
typedef struct FCB {
    char filename[9];
    char exname[4];
    unsigned char attribute;        // 0: 目录文件，1: 普通文件
    unsigned short time;
    unsigned short date;
    unsigned short first;           // 文件的第一块
    unsigned long length;           // 文件占用数据块数量
    char free;                      // 1 表示有效，0 表示空闲
} fcb;

/**
 * @description: FAT表目录项
 */
typedef struct FAT {
    unsigned short id;
} fat;

/**
 * @description: 用户打开表
 */
typedef struct USEROPEN {
    fcb open_fcb;                   // 打开的文件的 FCB 复制
    char dir[80];                   // 目录
    int count;                      // 光标位置
    char fcb_state;                 // 修改与否，若修改了，最后关掉的时候需要同步
    char free;
} useropen;

/**
 * @description: 全局变量
 */
unsigned char *fs_head;             // 新建虚拟磁盘首地址
useropen openfile_list[MAX_OPENFILE];   // 用户打开表
int curdir;                         // 当前打开文件的在上述表中的下标
char current_dir[80];               // 当前目录
unsigned char *start;               // 数据开始块

/**
 * @description: 函数声明
 */
int start_sys(void);

int my_format(char **args);

int do_format(void);

int my_cd(char **args);

void do_chdir(int fd);

int my_pwd(char **args);

int my_mkdir(char **args);

int do_mkdir(const char *parpath, const char *dirname);

int my_rmdir(char **args);

void do_rmdir(fcb *dir);

int my_ls(char **args);

void do_ls(int first, char mode);

int my_create(char **args);

int do_create(const char *parpath, const char *filename);

int my_rm(char **args);

void do_rm(fcb *file);

int my_open(char **args);

int do_open(char *path);

int my_close(char **args);

void do_close(int fd);

int my_write(char **args);

int do_write(int fd, char *content, size_t len, int wstyle);

int my_read(char **args);

int do_read(int fd, int len, char *text);

int my_exit_sys();

int get_free(int count);

int set_free(unsigned short first, unsigned short length, int mode);

int set_fcb(fcb *f, const char *filename, const char *exname, unsigned char attr, unsigned short first,
            unsigned long length,
            char ffree);

int my_openlist(char **args);

unsigned short get_time(struct tm *timeinfo);

unsigned short get_date(struct tm *timeinfo);

fcb * fcb_cpy(fcb *dest, fcb *src);

char * get_abspath(char *abspath, const char *relpath);

int get_useropen();

fcb *find_fcb(const char *path);

fcb *find_fcb_r(char *token, int root);

void init_folder(int first, int second);

void get_fullname(char *fullname, fcb *fcb1);

char *trans_date(char *sdate, unsigned short date);

char *trans_time(char *stime, unsigned short time);