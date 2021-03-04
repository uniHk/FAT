/*
 * @Author: uniHk
 * @Date: 2020-12-14 18:38:05
 * @LastEditTime: 2020-12-24 15:11:07
 */

#include "fs.h"

int start_sys(void) {
    fs_head = (unsigned char *)malloc(DISK_SIZE);
    memset(fs_head, 0, DISK_SIZE);
    FILE *fp = NULL;
    
    if ((fp = fopen(SYS_PATH, "r")) != NULL) {      // 重启
        fread(fs_head, DISK_SIZE, 1, fp);           // 读一次，一次读完
        fclose(fp);
    }
    else {                                          // 第一次启动
        printf("System is not initialized, now install it and create system file.\n");
        printf("Please don't leave the program.\n");
        do_format();
        printf("Initialized successfully\n");
    }

    // 初始化第一个打开文件
    fcb_cpy(&openfile_list[0].open_fcb, ((fcb *)(fs_head + 5 * BLOCK_SIZE)));   // 把根目录打开
    strcpy(openfile_list[0].dir, ROOT);             // 目前只修改打开的根目录文件，同步不急
    openfile_list[0].count = 0;
    openfile_list[0].fcb_state = 0;
    openfile_list[0].free = 1;
    curdir = 0;

    // 初始化剩余打开文件
    fcb *empty =  (fcb *)malloc(sizeof(fcb));
    set_fcb(empty, "\0", "\0", 0, 0, 0, 0);
    for (int i = 1; i < MAX_OPENFILE; ++i) {
        fcb_cpy(&openfile_list[i].open_fcb, empty);
        openfile_list[i].count = 0;
        openfile_list[i].fcb_state = 0;
        openfile_list[i].free = 0;
    }

    // 初始化全局变量
    strcpy(current_dir, openfile_list[curdir].dir);
    start = ((block0 *)fs_head)->start_block;
    free(empty);

    return 0;
}

int my_format(char **args) {
    int i;
    for (i = 0; args[i] != NULL; ++i);
    if (i > 2) {
        fprintf(stderr, "format: expected argument to \"format\"\n");
        return 1;
    }

    do_format();
    return 1;
}

int do_format(void) {
    // 初始化
    unsigned char *ptr = fs_head;
    block0 *init_block = (block0 *)ptr;
    strcpy(init_block->information,
            "Disk Size = 1MB, Block Size = 1KB, Block0 in 0, FAT0/1 in 1/3, Root Directory in 5");
    init_block->root = 5;
    init_block->start_block = (unsigned char *)(init_block + BLOCK_SIZE * 7);

    // 初始化 FAT0/1
    ptr += BLOCK_SIZE;
    set_free(0, 0, 2);

    // 在 FAT 表上分配前 5 个块
    set_free(get_free(1), 1, 0);
    set_free(get_free(2), 2, 0);
    set_free(get_free(2), 2, 0);

    // 处理根目录
    ptr += BLOCK_SIZE * 4;
    fcb *root = (fcb *)ptr;
    int first = get_free(ROOT_BLOCK_NUM);
    set_free(first, ROOT_BLOCK_NUM, 0);
    set_fcb(root, ".", "di", 0, first, BLOCK_SIZE * 2, 1);
    root++;
    set_fcb(root, "..", "di", 0, first, BLOCK_SIZE * 2, 1);
    root++;
    for (int i = 2; i < BLOCK_SIZE * 2 / sizeof(fcb); ++i, ++root) {
        root->free = 0;
    }

    // 写回
    FILE *fp = fopen(SYS_PATH, "w");
    fwrite(fs_head, DISK_SIZE, 1, fp);
    fclose(fp);

    return 0;
}

int my_cd(char **args) {
    int i;
    for (i = 0; args[i] != NULL; ++i);
    if (i != 2) {
        fprintf(stderr, "cd: expected argument to \"cd\"\n");
        return 1;
    }

    // 检查参数
    char abspath[PATHLENGTH];
    memset(abspath, 0, PATHLENGTH);
    get_abspath(abspath, args[1]);
    
    // printf("abspath: %s\n", abspath);
    
    fcb *dir = find_fcb(abspath);
    if (dir == NULL || dir->attribute == 1) {
        fprintf(stderr, "cd: No such folder\n");
        return 1;
    }

    // 如果处于打开状态
    for (int i = 0; i < MAX_OPENFILE; ++i) {
        if (openfile_list[i].free == 0)
            continue;
        if (!strcmp(dir->filename, openfile_list[i].open_fcb.filename) &&
            dir->first == openfile_list[i].open_fcb.first) {
            do_chdir(i);
            return 1;
        }
    }

    // 否则先打开，再 cd 过去
    int fd;
    if ((fd = do_open(abspath)) > 0)
        do_chdir(fd);
    
    return 1;
}

void do_chdir(int fd) {
    curdir = fd;
    memset(current_dir, 0, sizeof(current_dir));
    strcpy(current_dir, openfile_list[curdir].dir);
}

int my_pwd(char **args) {
    if (args[1] != NULL) {
        fprintf(stderr, "pwd: too many arguments\n");
        return 1;
    }

    printf("%s\n", current_dir);
    return 1;
}

int my_mkdir(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "mkdir: missing operand\n");
        return 1;
    }

    char path[PATHLENGTH], parpath[PATHLENGTH], dirname[NAMELENGTH];
    for (int i = 1; args[i] != NULL; ++i) {
        get_abspath(path, args[i]);
        char *end = strrchr(path, '/');
        if (end == path) {
            strcpy(parpath, "/");                   // 根目录要手动补一下 '/'
            strcpy(dirname, path + 1);
        }
        else {
            strncpy(parpath, path, end - path);
            parpath[end-path] = 0;
            strcpy(dirname, end + 1);
        }
        if (find_fcb(parpath) == NULL) {            // 父目录不存在，路径有问题
            fprintf(stderr, "create: cannot create \'%s\': Parent folder not exists\n", parpath);
            continue;
        }
        if (find_fcb(path) != NULL) {               // 该目录下有同名目录了，失败
            fprintf(stderr, "create: cannot create \'%s\': Folder or file exists\n", args[i]);
            continue;
        }

        do_mkdir(parpath, dirname);
    }

    return 1;
}

int do_mkdir(const char *parpath, const char *dirname) {
    int second = get_free(1);
    int flag = 0, first = find_fcb(parpath)->first;
    fcb *dir = (fcb *)(fs_head + BLOCK_SIZE * first);

    // 检查该目录下是否有空闲 fcb
    for (int i = 0; i < BLOCK_SIZE / sizeof(fcb); ++i, ++dir) {
        if (dir->free == 0) {
            flag = 1;
            break;
        }
    }
    if (!flag) {
        fprintf(stderr, "mkdir: Cannot create more file in %s\n", parpath);
        return -1;
    }

    // 检查是否有空闲盘块
    if (second == -1) {
        fprintf(stderr, "mkdir: No more space\n");
        return -1;
    }
    set_free(second, 1, 0);

    // 初始化新目录
    set_fcb(dir, dirname, "di", 0, second, BLOCK_SIZE, 1);
    init_folder(first, second);
    return 0;
}

int my_rmdir(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "rmdir: missing operand\n");
        return 1;
    }

    for (int i = 1; args[i] != NULL; ++i) {
        if (!strcmp(args[i], ".") || !strcmp(args[i], "..")) {
            fprintf(stderr, "rmdir: cannot remove %s: '.' or '..' is read only \n", args[i]);
            return 1;
        }

        if (!strcmp(args[i], "/")) {                // 想删我根目录？自然是不可能的
            fprintf(stderr, "rmdir:  Permission denied\n");
            return 1;
        }

        fcb *dir = find_fcb(args[i]);
        if (dir == NULL) {
            fprintf(stderr, "rmdir: cannot remove %s: No such folder\n", args[i]);
            return 1;
        }

        if (dir->attribute == 1) {
            fprintf(stderr, "rmdir: cannot remove %s: Is a directory\n", args[i]);
            return 1;
        }

        for (int j = 0; j < MAX_OPENFILE; ++j) {
            if (openfile_list[j].free == 0)
                continue;
            // 打开的目录项就删不了咯
            if (!strcmp(dir->filename, openfile_list[j].open_fcb.filename) &&
                dir->first == openfile_list[j].open_fcb.first) {
                fprintf(stderr, "rmdir: cannot remove %s: File is open\n", args[i]);
                return 1;
            }
        }

        do_rmdir(dir);
    }
    return 1;
}

void do_rmdir(fcb *dir) {
    int first = dir->first;
    
    dir->free = 0;
    dir = (fcb *)(fs_head + BLOCK_SIZE * first);
    dir->free = 0;                                  // .目录设为空
    dir++;
    dir->free = 0;                                  // ..目录也设为空

    set_free(first, 1, 1);                          // 更新 FAT 表
}

int my_ls(char **args) {
    int first = openfile_list[curdir].open_fcb.first;
    int i, mode = 'n';
    int flag[3];

    for (i = 0; args[i] != NULL; ++i)
        flag[i] = 0;
    if (i > 3) {
        fprintf(stderr, "ls: expected argument\n");
        return 1;
    }

    // 检查是否需要 -l 模式
    flag[0] = 1;
    for (i = 1; args[i] != NULL; ++i) {
        if (args[i][0] == '-') {
            flag[i] = 1;
            if (!strcmp(args[i], "-l")) {
                mode = 'l';
                break;
            }
            else {
                fprintf(stderr, "ls: wrong operand\n");
                return 1;
            }
        }
    }

    for (i = 1; args[i] != NULL; ++i) {
        if (flag[i] == 0) {
            fcb *dir = find_fcb(args[i]);
            if (dir != NULL && dir->attribute == 0) {
                first = dir->first;
            }
            else {
                fprintf(stderr, "ls: cannot access '%s': No such file or directory\n", args[i]);
                return 1;
            }
            break;
        }
    }

    do_ls(first, mode);
    
    return 1;
}

void do_ls(int first, char mode) {
    int length = BLOCK_SIZE;
    char fullname[NAMELENGTH], date[16], time[16];
    fcb *root = (fcb *)(fs_head + BLOCK_SIZE * first);
    block0 *init_block = (block0 *)fs_head;

    // 如果该目录为根目录，则 length 变为 2 倍
    if (first == init_block->root) {
        length = ROOT_BLOCK_NUM * BLOCK_SIZE;
    }

    if (mode == 'n') {
        for (int i = 0, count = 1; i < length / sizeof(fcb); ++i, ++count, ++root) {
            if (root->free == 0)
                continue;
            
            if (root->attribute == 0) {
                printf("%s", FOLDER_COLOR);
                printf("%s\t", root->filename);
                printf("%s", DEFAULT_COLOR);
            }
            else {
                get_fullname(fullname, root);
                printf("%s\t", fullname);
            }
            if (count % 5 == 0) {
                printf("\n");
            }
        }
    }
    else if (mode == 'l') {
        for (int i = 0; i < length / sizeof(fcb); ++i, ++root) {
            if (root->free == 0)
                continue;
            trans_date(date, root->date);
            trans_time(time, root->time);
            get_fullname(fullname, root);
            printf("%d\t%6d\t%6ld\t%s\t%s\t", root->attribute, root->first, root->length, date, time);
            if (root->attribute == 0) {
                printf("%s", FOLDER_COLOR);
                printf("%s\n", fullname);
                printf("%s", DEFAULT_COLOR);
            }
            else {
                printf("%s\n", fullname);
            }
        }
    }
    printf("\n");
}

int my_create(char **args) {
    char path[PATHLENGTH], parpath[PATHLENGTH], filename[NAMELENGTH];
    char *end = NULL;

    if (args[1] == NULL) {
        fprintf(stderr, "create: missing operand\n");
        return 1;
    }

    memset(parpath, 0, PATHLENGTH);
    memset(filename, 0, NAMELENGTH);

    for (int i = 1; args[i] != NULL; ++i) {
        get_abspath(path, args[i]);
        end = strrchr(path, '/');
        if (end == path) {
            strcpy(parpath, "/");                   // 根目录要手动补一下 '/'
            strcpy(filename, path + 1);
        }
        else {
            strncpy(parpath, path, end - path);
            strcpy(filename, end + 1);
        }

        if (find_fcb(parpath) == NULL) {            // 父目录不存在，路径有问题
            fprintf(stderr, "create: cannot create \'%s\': Parent folder not exists\n", parpath);
            continue;
        }
        if (find_fcb(path) != NULL) {               // 该目录下有同名文件了，失败
            fprintf(stderr, "create: cannot create \'%s\': Folder or file exists\n", args[i]);
            continue;
        }

        do_create(parpath, filename);
    }

    return 1;
}

int do_create(const char *parpath, const char *filename) {
    char fullname[NAMELENGTH], fname[16], exname[8];
    char *token = NULL;
    int first = get_free(1), flag = 0;
    fcb *dir = (fcb *)(fs_head + BLOCK_SIZE * find_fcb(parpath)->first);

    // 检查该目录下是否有空闲 fcb
    for (int i = 0; i < BLOCK_SIZE / sizeof(fcb); ++i, ++dir) {
        if (dir->free == 0) {
            flag = 1;
            break;
        }
    }
    if (!flag) {
        fprintf(stderr, "create: Cannot create more file in %s\n", parpath);
        return -1;
    }

    // 检查是否有空闲盘块
    if (first == -1) {
        fprintf(stderr, "create: No more space\n");
        return -1;
    }
    set_free(first, 1, 0);

    // 把文件名和后缀名划分开
    memset(fullname, 0, NAMELENGTH);
    memset(fname, 0, 8);
    memset(exname, 0, 3);
    strcpy(fullname, filename);
    token = strtok(fullname, ".");
    strncpy(fname, token, 8);
    token = strtok(NULL, ".");
    if (token != NULL) {
        strncpy(exname, token, 3);
    }
    else {
        strncpy(exname, "d", 2);
    }

    set_fcb(dir, fname, exname, 1, first, 0, 1);

    return 0;
}

int my_rm(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "rm: missing operand\n");
        return 1;
    }

    for (int i = 1; args[i] != NULL; ++i) {
        fcb *file = find_fcb(args[i]);
        if (file == NULL) {
            fprintf(stderr, "rm: cannot remove %s: No such file\n", args[i]);
            return 1;
        }

        if (file->attribute == 0) {
            fprintf(stderr, "rm: cannot remove %s: Is a directory\n", args[i]);
            return 1;
        }

        // 检查是否打开，若打开，则不能直接删除，需要手动关闭再删除
        for (int j = 0; j < MAX_OPENFILE; ++j) {
            if (openfile_list[j].free == 0)
                continue;
            
            if (!strcmp(file->filename, openfile_list[j].open_fcb.filename) &&
                file->first == openfile_list[j].open_fcb.first) {
                fprintf(stderr, "rm: cannot remove %s: File is open\n", args[i]);
                return 1;
            }
        }

        do_rm(file);
    }
    
    return 1;
}

void do_rm(fcb *file) {
    int first = file->first;

    file->free = 0;                                 // 目录文件中的 FCB 删掉
    set_free(first, 0, 1);                          // 还要在 FAT 表中删除
}

int my_open(char **args) {
    char path[PATHLENGTH];

    if (args[1] == NULL) {
        fprintf(stderr, "open: missing operand\n");
        return 1;
    }
    
    // 看看有没有 -l
    if (args[1][0] == '-') {
        if (!strcmp(args[1], "-l")) {
            printf("fd filename exname state path\n");
            for (int i = 0; i < MAX_OPENFILE; ++i) {
                if (openfile_list[i].fcb_state == 0)
                    continue;
                printf("%2d %8s %-6s %-5d %s\n", i, openfile_list[i].open_fcb.filename,
                        openfile_list[i].open_fcb.exname,
                        openfile_list[i].fcb_state, openfile_list[i].dir);
            }
            return 1;
        }
        else {
            fprintf(stderr, "open: wrong argument\n");
            return 1;
        }
    }

    for (int i = 1; args[i] != NULL; ++i) {
        fcb *file = find_fcb(args[i]);
        if (file == NULL) {
            fprintf(stderr, "open: cannot open %s: No such file or folder\n", args[i]);
            return 1;
        }

        for (int j = 0; j < MAX_OPENFILE; ++j) {
            if (openfile_list[j].free == 0)
                continue;
            
            if (!strcmp(file->filename, openfile_list[j].open_fcb.filename) &&
                file->first == openfile_list[j].open_fcb.first) {
                fprintf(stderr, "open: cannot open %s: File or folder is open\n", args[i]);
                continue;
            }
        }
        do_open(get_abspath(path, args[i]));
    }

    return 1;
}

int do_open(char *path) {
    int fd = get_useropen();
    fcb *file = find_fcb(path);
    if (fd == -1) {
        fprintf(stderr, "open: cannot open file, no more useropen entry\n");
        return -1;
    }
    fcb_cpy(&openfile_list[fd].open_fcb, file);     // 目录和普通文件一视同仁
    openfile_list[fd].free = 1;
    openfile_list[fd].count = 0;
    openfile_list[fd].fcb_state = 0;
    memset(openfile_list[fd].dir, 0, 80);
    strcpy(openfile_list[fd].dir, path);

    return fd;
}

/**
 * @description: -a 表示关闭所有打开文件——除了 当前目录文件
 */
int my_close(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "close: missing operand\n");
        return 1;
    }

    // 检查是否有 -a
    if (args[1][0] == '-') {
        if (!strcmp(args[1], "-a")) {
            for (int i = 0; i < MAX_OPENFILE; ++i) {
                if (i == curdir)
                    continue;
                openfile_list[i].free = 0;
            }
            return 1;
        }
        else {
            fprintf(stderr, "close: wrong argument\n");
            return 1;
        }
    }

    for (int i = 1; args[i] != NULL; ++i) {
        fcb *file = find_fcb(args[i]);
        if (file == NULL) {
            fprintf(stderr, "close: cannot close %s: No such file or folder\n", args[i]);
            return 1;
        }

        // 如果确实是打开的文件，则关闭掉；否则忽略
        for (int j = 0; j < MAX_OPENFILE; j++) {
            if (openfile_list[j].free == 0) {
                continue;
            }

            if (!strcmp(file->filename, openfile_list[j].open_fcb.filename) &&
                file->first == openfile_list[j].open_fcb.first) {
                do_close(j);
            }
        }
    }
    
    return 1;
}

void do_close(int fd) {
    if (openfile_list[fd].fcb_state == 1) {         // 如果有修改，则同步回去，写回
        fcb_cpy(find_fcb(openfile_list[fd].dir), &openfile_list[fd].open_fcb);
    }
    openfile_list[fd].free = 0;
}

/**
 * @description: 写操作
 * @param -w 截断写，-c 覆盖写，-a 追加写
 */
int my_write(char **args) {
    int mode = 'w', flag = 0, i;                    // 默认截断
    char path[PATHLENGTH], str[WRITE_SIZE];

    // 检查 写模式 和 操作数
    for (i = 1; args[i] != NULL; ++i) {
        if (args[i][0] == '-') {
            if      (!strcmp(args[i], "-w"))    mode = 'w';
            else if (!strcmp(args[i], "-c"))    mode = 'c';
            else if (!strcmp(args[i], "-a"))    mode = 'a';
            else {
                fprintf(stderr, "write: wrong argument\n");
                return 1;
            }
        }
        else {
            flag += 1 << i;
        }
    }
    if ((flag == 0) || (flag > 4) || (i > 3)) {     // 没有操作数 || 操作数大于1 || 模式数大于1
        fprintf(stderr, "write: wrong argument\n");
        return 1;
    }

    // 不能在目录文件上写，需要检查一下
    fcb *file = NULL;
    strcpy(path, args[flag >> 1]);
    if ((file = find_fcb(path)) == NULL) {
        fprintf(stderr, "write: File not exists\n");
        return 1;
    }
    if (file->attribute == 0) {
        fprintf(stderr, "write: cannot access a folder\n");
        return 1;
    }

    // 检查是否打开，打开的文件才能写，否则报错
    memset(str, 0, WRITE_SIZE);
    for (i = 0; i < MAX_OPENFILE; ++i) {
        if (openfile_list[i].free == 0)
            continue;
        
        if (!strcmp(file->filename, openfile_list[i].open_fcb.filename) &&
            file->first == openfile_list[i].open_fcb.first) {
            if (mode == 'c') {
                printf("Please input location: ");
                scanf("%d", &openfile_list[i].count);
                getchar();                          // 读掉一个后面的换行或者其他划分符号
            }

            int j = 0;
            char c;
            while (1) {
                for (; (str[j] = getchar()) != '\n'; ++j);
                j++;
                if ((c = getchar()) == '\n') break; // 连续两个换行就是 写 结束
                else str[j++] = c;
            }

            if (mode == 'c') {
                do_write(i, str, j - 1, mode);      // 覆盖写不需要最后的换行
            }
            else {
                do_write(i, str, j + 1, mode);      // 其他两种还需要补一个换行，因为确定在文件末
            }

            return 1;
        }
    }

    fprintf(stderr, "write: file is not open\n");   // 想写没打开文件，报错
    return 1;
}

int do_write(int fd, char *content, size_t len, int wstyle) {
    fat *fat0 = (fat *)(fs_head + BLOCK_SIZE);
    fat *fat1 = (fat *)(fs_head + 3 * BLOCK_SIZE);

    // 先读出来，在 txt 上操作好了，再一次性写回去，增加稳定性，且好操作
    char txt[WRITE_SIZE] = {0};
    int write = openfile_list[fd].count;
    openfile_list[fd].count = 0;
    do_read(fd, openfile_list[fd].open_fcb.length, txt);
    openfile_list[fd].count = write;
    int i = openfile_list[fd].open_fcb.first;
    char input[WRITE_SIZE] = {0};
    strncpy(input, content, len);

    // -w 截断写，-c 覆盖写，-a 追加写
    if (wstyle == 'w') {
        memset(txt, 0, WRITE_SIZE);
        strncpy(txt, input, len);
    }
    else if (wstyle == 'c') {
        strncpy(txt + openfile_list[fd].count, input, len);
    }
    else if (wstyle == 'a') {
        strncpy(txt + openfile_list[fd].open_fcb.length, input, len);
    }

    // 写入文件系统
    int length = strlen(txt);
    int num = (length - 1) / BLOCK_SIZE + 1;
    int num0 = num;

    while (num) {
        char buf[BLOCK_SIZE] = {0};
        memcpy(buf, &txt[(num0 - num) * BLOCK_SIZE], BLOCK_SIZE);
        unsigned char *p = fs_head + i * BLOCK_SIZE;
        memcpy(p, buf, BLOCK_SIZE);
        num = num - 1;
        if (num > 0) {
            fat *fat_cur = fat0 + i;
            if (fat_cur->id == END) {               // 如果原文件不够大了，则继续申请
                int nxt = get_free(1);

                if (nxt == -1) {
                    fprintf(stderr, "write: No more space\n");
                    return -1;
                }

                fat_cur->id = nxt;
                fat_cur = fat0 + nxt;
                fat_cur->id = END;
            }
            i = (fat0 + i)->id;
        }
    }

    // 把原文件多余的磁盘块给删掉
    if (fat0[i].id != END) {
        int j = fat0[i].id;
        fat0[i].id = END;
        i = j;
        while (fat0[i].id != END) {
            int nxt = fat0[i].id;
            fat0[i].id = FREE;
            i = nxt;
        }
        fat0[i].id = FREE;
    }

    // 备份一下
    memcpy(fat1, fat0, 2 * BLOCK_SIZE);
    openfile_list[fd].open_fcb.length = length;
    openfile_list[fd].fcb_state = 1;
    
    return strlen(input);
}

/**
 * @description: 读操作
 * @param -s 选择读，-a 全部读
 */
int my_read(char **args) {
    int i, flag = 0;
    int mode = 'a';                                 // 默认全读
    char path[PATHLENGTH], str[WRITE_SIZE];

    for (i = 1; args[i] != NULL; ++i) {
        if (args[i][0] == '-') {
            if (!strcmp(args[i], "-s")) {
                mode = 's';
            } else if (!strcmp(args[i], "-a")) {
                mode = 'a';
            } else {
                fprintf(stderr, "read: wrong argument\n");
                return 1;
            }
        } else {
            flag += 1 << i;
        }
    }
    if ((flag == 0) || (flag > 4) || i > 3) {
        fprintf(stderr, "read: wrong argument\n");
        return 1;
    }

    // 类似与写操作，读操作也只能对普通文件
    strcpy(path, args[flag >> 1]);
    fcb *file = NULL;
    if ((file = find_fcb(path)) == NULL) {
        fprintf(stderr, "read: File not exists\n");
        return 1;
    }
    if (file->attribute == 0) {
        fprintf(stderr, "read: cannot access a folder\n");
        return 1;
    }

    // 只能读打开的文件，想读没有打开的，则报错
    // 不建议帮助打开，会涉及到打开数量限定问题
    memset(str, 0, WRITE_SIZE);
    for (i = 0; i < MAX_OPENFILE; i++) {
        if (openfile_list[i].free == 0)
           continue;
        if (!strcmp(file->filename, openfile_list[i].open_fcb.filename) &&
            file->first == openfile_list[i].open_fcb.first) {
            int length;
            if (mode == 'a') {
                openfile_list[i].count = 0;         // 从头开始读
                length = UINT16_MAX;                // 无限长，do_read 会帮忙限定的
            }
            if (mode == 's') {
                printf("Please input location: ");
                scanf("%d", &openfile_list[i].count);
                printf("Please input length: ");
                scanf("%d", &length);
                printf("-----------------------\n");
            }
            do_read(i, length, str);
            fputs(str, stdout);

            return 1;
        }
    }

    fprintf(stderr, "read: file is not open\n");
    return 1;
}

/**
 * @description: 裸——读操作
 * @param 将打开的 fd 号文件读到 txt 里
 * @return 实际读的长度
 */
int do_read(int fd, int len, char *txt) {
    memset(txt, 0, WRITE_SIZE);

    if (len <= 0)
        return 0;
    
    // 确定 len 实际长度
    fat *fat0 = (fat *)(fs_head + BLOCK_SIZE);
    int location = 0;                               // txt 光标位置，非文件光标
    int length = len;
    int count = openfile_list[fd].count;

    if ((openfile_list[fd].open_fcb.length - count) < len) {    // 没那么多可以读
        length = openfile_list[fd].open_fcb.length - count;
    }

    // 一块一块读
    int i = openfile_list[fd].open_fcb.first;       // 维护一个当前物理磁盘块号
    int num = count / BLOCK_SIZE;
    
    for (int j = 0; j < num; ++j) {
        i = (fat0 + i)->id;
    }

    while (length) {
        char buf[BLOCK_SIZE];
        int count = openfile_list[fd].count;
        int off = count % BLOCK_SIZE;

        fcb *p = (fcb *)(fs_head + BLOCK_SIZE * i);
        memcpy(buf, p, BLOCK_SIZE);

        if ((off + length) <= BLOCK_SIZE) {         // 最后一点咯
            memcpy(&txt[location], &buf[off], length);
            openfile_list[fd].count = openfile_list[fd].count + length;
            location += length;
            length = 0;
        }
        else {
            int tmp = BLOCK_SIZE - off;             // 除了第一块，其余块 off 都为 0
            memcpy(&txt[location], &buf[off], tmp);
            openfile_list[fd].count += tmp;
            location += tmp;
            length -= tmp;
        }

        i = (fat0 + i)->id;
    }

    return location;
}

int my_exit_sys(void) {
    for (int i = 0; i < MAX_OPENFILE; ++i) {
        if (openfile_list[i].free == 1)             // 打开的文件才需要关闭，否则同步会出错
            do_close(i);
    }

    FILE *fp = fopen(SYS_PATH, "w");
    fwrite(fs_head, DISK_SIZE, 1, fp);
    free(fs_head);
    fclose(fp);

    return 0;
}

int get_free(int count) {
    unsigned char *ptr = fs_head;
    fat *fat0 = (fat *)(ptr + BLOCK_SIZE);
    int flag = 0;
    int fat[BLOCK_NUM];

    for (int i = 0; i < BLOCK_NUM; ++i, ++fat0)
        fat[i] = fat0->id;

    for (int i = 0, j; i < BLOCK_NUM - count; ++i) {
        for (j = i; j < i + count; ++j) {
            if (fat[j] > 0) {
                flag = 1;
                break;
            }
        }
        if (flag) flag = 0, i = j;
        else return i;
    }

    return -1;
}

int set_free(unsigned short first, unsigned short length, int mode) {
    fat *flag = (fat *)(fs_head + BLOCK_SIZE);
    fat *fat0 = (fat *)(fs_head + BLOCK_SIZE);
    fat *fat1 = (fat *)(fs_head + BLOCK_SIZE * 3);
    int i = first, offset;
    fat0 += first;
    fat1 += first;

    if (mode == 1) {
        // 清空 first 对应文件
        while (fat0->id != END) {
            offset = fat0->id - (fat0 - flag) / sizeof(fat);
            fat0->id = FREE;
            fat1->id = FREE;
            fat0 += offset;
            fat1 += offset;
        }
        fat0->id = FREE;
        fat1->id = FREE;
    }
    else if (mode == 2) {
        // 格式化 FAT 表
        for (i = 0; i < BLOCK_NUM; ++i, ++fat0, ++fat1) {
            fat0->id = FREE;
            fat1->id = FREE;
        }
    }
    else {
        for (; i < first + length - 1; ++i, ++fat0, ++fat1) {
            fat0->id = first + 1;                   // 这里随意分配一个非 0 值即可，因为用不到
            fat1->id = first + 1;
        }
        fat0->id = END;
        fat1->id = END;
    }

    return 0;
}

int set_fcb(fcb *f, const char *filename, const char *exname, unsigned char attr, unsigned short first,
            unsigned long length, char ffree) {
    time_t *now = (time_t *)malloc(sizeof(time_t));
    struct tm *timeinfo = NULL;
    time(now);
    timeinfo = localtime(now);

    memset(f->filename, 0, 9);
    memset(f->exname, 0, 4);
    strncpy(f->filename, filename, 8);
    strncpy(f->exname, exname, 3);
    f->attribute = attr;
    f->time = get_time(timeinfo);
    f->date = get_date(timeinfo);
    f->first = first;
    f->length = length;
    f->free = ffree;

    free(now);
    return 0;
}

/**
 * @description: 打开了哪些文件，方便删除
 */
int my_openlist(char **args) {
    char fullname[FILENAME_MAX];
    char path[PATHLENGTH];
    int cnt = 0;

    for (int i = 0; i < MAX_OPENFILE; ++i) {
        if (openfile_list[i].free == 0)
            continue;
        if (!strcmp(openfile_list[i].dir, "/"))     // 根目录就不用输出了
            continue;
        cnt++;

        if (openfile_list[i].open_fcb.attribute == 0) {
            printf("%s", FOLDER_COLOR);
            printf("%s\n", openfile_list[i].dir);
            printf("%s", DEFAULT_COLOR);
        }
        else {
            printf("%s\n", openfile_list[i].dir);
        }
    }

    if (!cnt)
        printf("No opening file.\n");

    return 1;
}

unsigned short get_time(struct tm *timeinfo) {
    int hour, min, sec;
    unsigned short result;

    hour = timeinfo->tm_hour;
    min = timeinfo->tm_min;
    sec = timeinfo->tm_sec;
    result = (hour << 11) + (min << 5) + (sec >> 1);

    return result;
}

unsigned short get_date(struct tm *timeinfo) {
    int year, mon, day;
    unsigned short result;

    year = timeinfo->tm_year;
    mon = timeinfo->tm_mon;
    day = timeinfo->tm_mday;
    result = (year << 9) + (mon << 5) + day;

    return result;
}

fcb *fcb_cpy(fcb *dest, fcb *src) {
    memset(dest->filename, 0, 9);
    memset(dest->exname, 0, 4);

    strcpy(dest->filename, src->filename);
    strcpy(dest->exname, src->exname);
    dest->attribute = src->attribute;
    dest->time = src->time;
    dest->date = src->date;
    dest->first = src->first;
    dest->length = src->length;
    dest->free = src->free;

    return dest;
}

/**
 * @description: 根据相对路径得到绝对路径
 */
char *get_abspath(char *abspath, const char *relpath) {
    // 如果 relpath 就是绝对路径了
    if (!strcmp(relpath, DELIM) || relpath[0] == '/') {
        strcpy(abspath, relpath);
        return 0;
    }

    char str[PATHLENGTH];
    char *token = NULL, *end = NULL;
    
    memset(abspath, 0, PATHLENGTH);
    strcpy(abspath, current_dir);
    strcpy(str, relpath);

    token = strtok(str, DELIM);
    do {
        if (!strcmp(token, "."))
            continue;
        if (!strcmp(token, "..")) {
            if (!strcmp(abspath, ROOT))
                continue;
            end = strrchr(abspath, '/');
            if (end == abspath) {                   // 往上跳是根目录
                strcpy(abspath, ROOT);
                continue;
            }
            memset(end, 0, 1);                      // 跳到父目录
            continue;
        }
        if (strcmp(abspath, "/")) {                 // 如果之前不是根目录，则需要补一个 '/' 进行划分
            strcat(abspath, DELIM);
        }
        strcat(abspath, token);
    } while ((token = strtok(NULL, DELIM)) != NULL);

    return abspath;
}

/**
 * @description: 根据路径找 FCB 的入口函数
 */
fcb *find_fcb(const char *path) {
    char abspath[PATHLENGTH];
    get_abspath(abspath, path);                     // 防止后续的 strtok 更改了原 path
    char *token = strtok(abspath, DELIM);
    if (token == NULL) {                            // 根目录
        return (fcb *)(fs_head + BLOCK_SIZE * 5);
    }
    return find_fcb_r(token, 5);
}

/**
 * @description: 根据路径递归找 FCB
 */
fcb *find_fcb_r(char *token, int first) {
    int i, length = BLOCK_SIZE;
    char fullname[NAMELENGTH] = "\0";
    fcb *root = (fcb *)(BLOCK_SIZE * first + fs_head);
    fcb *dir = NULL;
    block0 *init_block = (block0 *)fs_head;

    if (first == init_block->root) {                // 根目录长度为两倍
        length = ROOT_BLOCK_NUM * BLOCK_SIZE;
    }

    // 检测当前目录下所有 FCB
    for (i = 0, dir = root; i < length / sizeof(fcb); ++i, ++dir) {
        if (dir->free == 0)
            continue;
        get_fullname(fullname, dir);
        if (!strcmp(token, fullname)) {
            token = strtok(NULL, DELIM);
            if (token == NULL) {
                return dir;
            }
            return find_fcb_r(token, dir->first);
        }
    }

    return NULL;                                    // 上面没有递归入口，则说明没找到
}

/**
 * @description: 在用户打开表里找一个空闲位置
 */
int get_useropen() {
    for (int i = 0; i < MAX_OPENFILE; ++i) {
        if (openfile_list[i].free == 0) {
            return i;
        }
    }

    return -1;
}

void init_folder(int first, int second) {
    fcb *par = (fcb *)(fs_head + BLOCK_SIZE * first);
    fcb *cur = (fcb *)(fs_head + BLOCK_SIZE * second);

    set_fcb(cur, ".", "di", 0, second, BLOCK_SIZE, 1);
    cur++;
    set_fcb(cur, "..", "di", 0, first, par->length, 1); // par->length 可能为 2 * BLOCK_SIZE
    cur++;

    for (int i = 2; i < BLOCK_SIZE / sizeof(fcb); ++i, ++cur)
        cur->free = 0;
}

void get_fullname(char *fullname, fcb *fcb1) {
    memset(fullname, 0, NAMELENGTH);
    strcat(fullname, fcb1->filename);
    if (fcb1->attribute == 1) {
        strncat(fullname, ".", 1);
        strncat(fullname, fcb1->exname, 3);
    }
}

char *trans_date(char *sdate, unsigned short date) {
    int year, month, day;
    memset(sdate, 0, 16);

    year = date & 0xffff;                           // 1111 1111 1111 1111
    month = date & 0x01ff;                          // 0000 0001 1111 1111
    day = date & 0x001f;                            // 0000 0000 0001 1111

    sprintf(sdate, "%04d-%02d-%02d", (year >> 9) + 1900, (month >> 5) + 1, day);
    return sdate;
}

char *trans_time(char *stime, unsigned short time) {
    int hour, min, sec;
    memset(stime, 0, 16);

    hour = time & 0xffff;                           // 1111 1111 1111 1111
    min = time & 0x07ff;                            // 0000 0111 1111 1111
    sec = time & 0x001f;                            // 0000 0000 0001 1111

    sprintf(stime, "%02d:%02d:%02d", hour >> 11, min >> 5, sec << 1);
    return stime;
}