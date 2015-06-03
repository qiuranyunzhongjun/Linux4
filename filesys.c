﻿#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include "filesys.h"

int fd;
struct BootDescriptor_t hdr;
struct Entry *curdir = NULL;//当前所在的目录，默认NULL表示位于根目录
int dirno = 0;/*代表目录的层数*/
struct Entry* fatherdir[10];
unsigned char fatbuf[512*250];

int fat_offset;
int fat_size;
int rootdir_offset;

#define RevByte(l,h) ((l)|(h)<<8)
#define RevWord(lowest,lower,higher,highest) ((highest)<< 24|(higher)<<16|(lower)<<8|lowest)

/*
 *读取BootSector，获取FAT16的格式信息
 *打印启动项记录
 */
void ScanBootSector()
{
    unsigned char buf[64];
    int ret,i;

    /* read first 64 bytes of boot sector */
    if((ret = read(fd,buf,sizeof buf))<0)
        err(1, "read boot sector failed");

    for (i=0; i<8; i++)
        hdr.Oem_name[i] = buf[i+3];
    hdr.Oem_name[i] = 0;

    unsigned char *p = buf+11;

#define BYTE *p++
#define WORD *p++|*p++<<8
    hdr.BytesPerSector = WORD;
    hdr.SectorsPerCluster = BYTE;
    hdr.ReservedSectors = WORD;
    hdr.FATs = BYTE;
    hdr.RootDirEntries = WORD;
    hdr.LogicSectors = WORD;
    hdr.MediaType = BYTE;
    hdr.SectorsPerFAT = WORD;
    hdr.SectorsPerTrack = WORD;
    hdr.Heads = WORD;
    hdr.HiddenSectors = WORD;
#undef WORD
#undef BYTE

    rootdir_offset = hdr.BytesPerSector +
        hdr.FATs * hdr.SectorsPerFAT * hdr.BytesPerSector;

    fat_offset = hdr.BytesPerSector * hdr.ReservedSectors;
    fat_size = hdr.BytesPerSector * hdr.SectorsPerFAT;

#define PRINTS(x) printf("%-24s%s\n", #x, hdr.x)
#define PRINTD(x) printf("%-24s%d\n", #x, hdr.x)
    PRINTS(Oem_name);
    PRINTD(BytesPerSector);
    PRINTD(SectorsPerCluster);
    PRINTD(ReservedSectors);
    PRINTD(FATs);
    PRINTD(RootDirEntries);
    PRINTD(LogicSectors);
    PRINTD(MediaType);
    PRINTD(SectorsPerFAT);
    PRINTD(SectorsPerTrack);
    PRINTD(Heads);
    PRINTD(HiddenSectors);
#undef PRINTD
#undef PRINTS

    lseek(fd, hdr.BytesPerSector-64, SEEK_CUR);
}

/*日期*/
void findDate(unsigned short *year,
              unsigned short *month,
              unsigned short *day,
              unsigned char info[2])
{
    int date;
    date = RevByte(info[0],info[1]);

    *year = ((date & MASK_YEAR)>> 9 )+1980;
    *month = ((date & MASK_MONTH)>> 5);
    *day = (date & MASK_DAY);
}

/*时间*/
void findTime(unsigned short *hour,
              unsigned short *min,
              unsigned short *sec,
              unsigned char info[2])
{
    int time;
    time = RevByte(info[0],info[1]);

    *hour = ((time & MASK_HOUR )>>11);
    *min = (time & MASK_MIN)>> 5;
    *sec = (time & MASK_SEC) * 2;
}

/*
 *文件名格式化，便于比较
 */
void FileNameFormat(unsigned char *name)
{
    unsigned char *p = name;
    while(*p!='\0')
        p++;
    p--;
    while(*p==' ')
        p--;
    p++;
    *p = '\0';
}

/*参数：entry，类型：struct Entry*
 *返回值：成功，则返回偏移值；失败：返回负值
 *功能：从根目录或文件簇中得到文件表项
 */
int GetEntry(struct Entry *pentry)
{
    int ret,i;
    int count = 0;
    unsigned char buf[DIR_ENTRY_SIZE], info[2];

    /*读一个目录表项，即32字节*/
    if( (ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
        err(1, "read entry failed");
    count += ret;

    if(buf[0]==0xe5 || buf[0]== 0x00)
        return -1*count;
    else
    {
        /*长文件名，忽略掉*/
        while (buf[11]== 0x0f)
        {
            if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
                err(1, "read root dir failed");
            count += ret;
        }

        /*命名格式化，主义结尾的'\0'*/
        for (i=0 ;i<=10;i++)
            pentry->short_name[i] = buf[i];
        pentry->short_name[i] = '\0';
        //去掉文件名结尾的空格
        FileNameFormat(pentry->short_name);


        //以下两个还原文件时间的函数有问题，请参照pdf文档修正
        info[0]=buf[22];
        info[1]=buf[23];
        findTime(&(pentry->hour),&(pentry->min),&(pentry->sec),info);

        info[0]=buf[24];
        info[1]=buf[25];
        findDate(&(pentry->year),&(pentry->month),&(pentry->day),info);

        pentry->FirstCluster = RevByte(buf[26],buf[27]);
        pentry->size = RevWord(buf[28],buf[29],buf[30],buf[31]);

        pentry->readonly = (buf[11] & ATTR_READONLY) ?1:0;
        pentry->hidden = (buf[11] & ATTR_HIDDEN) ?1:0;
        pentry->system = (buf[11] & ATTR_SYSTEM) ?1:0;
        pentry->vlabel = (buf[11] & ATTR_VLABEL) ?1:0;
        pentry->subdir = (buf[11] & ATTR_SUBDIR) ?1:0;
        pentry->archive = (buf[11] & ATTR_ARCHIVE) ?1:0;

        return count;
    }
}

/*
 *功能：显示当前目录的内容
 *返回值：1，成功；-1，失败
 */
int fd_ls()
{
    int ret, offset,cluster_addr;
    struct Entry entry;
    unsigned char buf[DIR_ENTRY_SIZE];
    if( (ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
        err(1, "read entry failed");
    if(curdir==NULL)
        printf("Root_dir\n");
    else
        printf("%s_dir\n",curdir->short_name);
    printf("\tname\tdate\t\t time\t\tcluster\tsize\t\tattr\n");

    if(curdir==NULL)  /*显示根目录区*/
    {
        /*将fd定位到根目录区的起始地址*/
        if((ret= lseek(fd,rootdir_offset,SEEK_SET))<0)
            err(1, "lseek rootdir_offset failed");

        offset = rootdir_offset;

        /*从根目录区开始遍历，直到数据区起始地址*/
        while(offset < (DATA_OFFSET))
        {
            ret = GetEntry(&entry);

            offset += abs(ret);
            if(ret > 0)
            {
                printf("%12s\t"
                       "%d:%d:%d\t"
                       "%d:%d:%d   \t"
                       "%d\t"
                       "%d\t\t"
                       "%s\n",
                       entry.short_name,
                       entry.year,entry.month,entry.day,
                       entry.hour,entry.min,entry.sec,
                       entry.FirstCluster,
                       entry.size,
                       (entry.subdir) ? "dir":"file");
            }
        }
    }

    else /*显示子目录*/
    {
        cluster_addr = DATA_OFFSET + (curdir->FirstCluster-2) * CLUSTER_SIZE ;
        if((ret = lseek(fd,cluster_addr,SEEK_SET))<0)
            err(1, "lseek cluster_addr failed");

        offset = cluster_addr;

        /*只读一簇的内容*/
        while(offset<cluster_addr +CLUSTER_SIZE)
        {
            ret = GetEntry(&entry);
            offset += abs(ret);
            if(ret > 0)
            {
                printf("%12s\t"
                       "%d:%d:%d\t"
                       "%d:%d:%d   \t"
                       "%d\t"
                       "%d\t\t"
                       "%s\n",
                       entry.short_name,
                       entry.year,entry.month,entry.day,
                       entry.hour,entry.min,entry.sec,
                       entry.FirstCluster,
                       entry.size,
                       (entry.subdir) ? "dir":"file");
            }
        }
    }
    return 0;
}


/*
 *参数：entryname 类型：char
 ：pentry    类型：struct Entry*
 ：mode      类型：int，mode=1，为目录表项；mode=0，为文件
 *返回值：偏移值大于0，则成功；-1，则失败
 *功能：搜索当前目录，查找名为entryname的文件或目录项，如果没找到但会-1，找到了返回目标文件在硬盘中的偏移
 */
int ScanEntry (char *entryname,struct Entry *pentry,int mode)
{
    int ret,offset,i;
    int cluster_addr;
    char uppername[80];
    for(i=0;i< strlen(entryname);i++)
        uppername[i]= toupper(entryname[i]);
    uppername[i]= '\0';
    /*扫描根目录*/
    if(curdir ==NULL)
    {
        if((ret = lseek(fd,rootdir_offset,SEEK_SET))<0)
            err(1, "lseek rootdir_offset failed");
        offset = rootdir_offset;


        while(offset<DATA_OFFSET)
        {
            ret = GetEntry(pentry);
            offset +=abs(ret);

            if(pentry->subdir == mode &&!strcmp((char*)pentry->short_name,uppername))

                return offset;

        }
        return -1;
    }

    /*扫描子目录*/
    else
    {
        cluster_addr = DATA_OFFSET + (curdir->FirstCluster -2)*CLUSTER_SIZE;
        if((ret = lseek(fd,cluster_addr,SEEK_SET))<0)
            err(1, "lseek cluster_addr failed");
        offset= cluster_addr;

        while(offset<cluster_addr + CLUSTER_SIZE)
        {
            ret= GetEntry(pentry);
            offset += abs(ret);
            if(pentry->subdir == mode &&!strcmp((char*)pentry->short_name,uppername))
                return offset;
        }
        return -1;
    }
}



/*
 *参数：dir，类型：char
 *返回值：1，成功；-1，失败
 *功能：改变目录到父目录或子目录
 */
int fd_cd(char *dir)
{
    struct Entry *pentry;
    int ret;

    if(!strcmp(dir,"."))
    {
        return 1;
    }
    if(!strcmp(dir,"..") && curdir==NULL)
        return 1;
    /*返回上一级目录*/
    if(!strcmp(dir,"..") && curdir!=NULL)
    {
        //fatherdir 用于保存父目录信息。
        curdir = fatherdir[dirno];
        dirno--;
        return 1;
    }
    //注意此处有内存泄露
    pentry = (struct Entry*)malloc(sizeof(struct Entry));

    ret = ScanEntry(dir,pentry,1);
    if(ret < 0)
    {
        printf("no such dir\n");
        free(pentry);
        return -1;
    }
    dirno ++;
    fatherdir[dirno] = curdir;
    curdir = pentry;
    return 1;
}

/*
 *参数：prev，类型：unsigned char
 *返回值：下一簇
 *在fat表中获得下一簇的位置
 */
unsigned short GetFatCluster(unsigned short prev)
{
    unsigned short next;
    int index;

    index = prev * 2;
    next = RevByte(fatbuf[index],fatbuf[index+1]);

    return next;
}

/*
 *参数：cluster，类型：unsigned short
 *返回值：void
 *功能：清除fat表中的簇信息
 */
void ClearFatCluster(unsigned short cluster)
{
    int index;
    index = cluster * 2;
    //*2是因为我们用16bytes来表示一个cluster，这16字节是按照8字节8字节存入fatbuf中
    //所以每次更新要将连着的两个清0
    fatbuf[index]=0x00;
    fatbuf[index+1]=0x00;

}

void WriteFat(void)
{
    int i;
    lseek(fd, fat_offset, SEEK_SET);
    for (i=0; i<hdr.FATs; i++)
        write(fd, fatbuf, fat_size);
}

/*
 *读fat表的信息，存入fatbuf[]中
 */
void ReadFat(void)
{
    lseek(fd, fat_offset, SEEK_SET);
    read(fd, fatbuf, fat_size);
}


/*
 *参数：filename，类型：char
 *返回值：1，成功；-1，失败
 *功能;删除当前目录下的文件
 */
int fd_df(char *filename)
{
    struct Entry *pentry;
    int ret;
    unsigned char c;
    unsigned short seed,next;

    pentry = (struct Entry*)malloc(sizeof(struct Entry));

    /*扫描当前目录查找文件*/
    ret = ScanEntry(filename,pentry,0);
    if(ret<0)
    {
        printf("no such file\n");
        free(pentry);
        return -1;
    }

    /*清除fat表项*/
    seed = pentry->FirstCluster;
    while((next = GetFatCluster(seed))!=0xffff)
    {
        ClearFatCluster(seed);
        seed = next;

    }

    ClearFatCluster( seed );

    /*清除目录表项*/
    c=0xe5;//e5表示该目录项可用

    //现将文件指针定位到目录处，0x20等价于32，因为每条目录表项32bytes
    if(lseek(fd,ret-0x20,SEEK_SET)<0)
        err(1, "lseek fd_df failed");
    //标记目录表项可用
    if(write(fd,&c,1)<0)
        err(1, "write failed");

    /*
       这段话在源程序中存在，但助教感觉这句话是错的。。。o(╯□╰)o
       如果发现助教的感觉错了赶紧告诉助教，有加分！！
       if(lseek(fd,ret-0x40,SEEK_SET)<0)
       err(1, "lseek fd_df failed");
       if(write(fd,&c,1)<0)
       err(1, "write failed");*/

    free(pentry);
    WriteFat();
    return 1;
}


/*
 *参数：filename，类型：char，创建文件的名称
 size，    类型：int，文件的大小
 *返回值：1，成功；-1，失败
 *功能：在当前目录下创建文件
 */
int fd_cf(char *filename,int size)
{

    struct Entry *pentry;
    int ret,i=0,cluster_addr,offset;
    unsigned short cluster,clusterno[100];
    unsigned char c[DIR_ENTRY_SIZE];
    int index,clustersize;
    unsigned char buf[DIR_ENTRY_SIZE];
    pentry = (struct Entry*)malloc(sizeof(struct Entry));


    clustersize = (size / (CLUSTER_SIZE));

    if(size % (CLUSTER_SIZE) != 0)
        clustersize ++;

    //扫描根目录，是否已存在该文件名
    ret = ScanEntry(filename,pentry,0);
    if (ret<0)
    {
        /*查询fat表，找到空白簇，保存在clusterno[]中*/
        for(cluster=2;cluster<1000;cluster++)
        {
            index = cluster *2;
            if(fatbuf[index]==0x00&&fatbuf[index+1]==0x00)
            {
                clusterno[i] = cluster;

                i++;
                if(i==clustersize)
                    break;

            }

        }

        /*在fat表中写入下一簇信息*/
        for(i=0;i<clustersize-1;i++)
        {
           index = clusterno[i]*2;

            fatbuf[index] = (clusterno[i+1] &  0x00ff);
            fatbuf[index+1] = ((clusterno[i+1] & 0xff00)>>8);


        }
        /*最后一簇写入0xffff*/
        index = clusterno[i]*2;
        fatbuf[index] = 0xff;
        fatbuf[index+1] = 0xff;

        if(curdir==NULL)  /*往根目录下写文件*/
        {

            if((ret= lseek(fd,rootdir_offset,SEEK_SET))<0)
                err(1, "lseek rootdir_offset failed");
            offset = rootdir_offset;
            while(offset < DATA_OFFSET)
            {
                //读取一个条目
                if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
                    err(1, "read entry failed");

                offset += abs(ret);
                //看看条目是否可用（e5）或者是不是表示后面没有更多条目（00）
                if(buf[0]!=0xe5&&buf[0]!=0x00)
                {
                    //buf[11]是attribute，但是感觉下面这个while循环并没有什么卵用。。。
                    while(buf[11] == 0x0f)
                    {
                        if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
                            err(1, "read root dir failed");
                        offset +=abs(ret);
                    }
                }


                /*找出空目录项或已删除的目录项*/
                else
                {
                    offset = offset-abs(ret);
                    for(i=0;i<=strlen(filename);i++)
                    {
                        c[i]=toupper(filename[i]);
                    }
                    for(;i<=10;i++)
                        c[i]=' ';

                    c[11] = 0x01;

                    /*写第一簇的值*/
                    c[26] = (clusterno[0] &  0x00ff);
                    c[27] = ((clusterno[0] & 0xff00)>>8);

                    /*写文件的大小*/
                    c[28] = (size &  0x000000ff);
                    c[29] = ((size & 0x0000ff00)>>8);
                    c[30] = ((size& 0x00ff0000)>>16);
                    c[31] = ((size& 0xff000000)>>24);

                    /*还有很多内容并没有写入，大家请自己补充*/
                    /*而且这里还有个问题，就是对于目录表项的值为00的情况处理的不好*/
                    if(lseek(fd,offset,SEEK_SET)<0)
                        err(1, "lseek fd_cf failed");
                    if(write(fd,&c,DIR_ENTRY_SIZE)<0)
                        err(1, "write failed");




                    free(pentry);
                    WriteFat();

                    return 1;
                }

            }
        }
        else
        {
            //子目录的情况与根目录类似
            cluster_addr = (curdir->FirstCluster -2 )*CLUSTER_SIZE + DATA_OFFSET;
            if((ret= lseek(fd,cluster_addr,SEEK_SET))<0)
                err(1, "lseek cluster_addr failed");
            offset = cluster_addr;
            while(offset < cluster_addr + CLUSTER_SIZE)
            {
                if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
                    err(1, "read entry failed");

                offset += abs(ret);

                if(buf[0]!=0xe5&&buf[0]!=0x00)
                {
                    while(buf[11] == 0x0f)
                    {
                        if((ret = read(fd,buf,DIR_ENTRY_SIZE))<0)
                            err(1, "read root dir failed");
                        offset +=abs(ret);
                    }
                }
                else
                {
                    offset = offset - abs(ret);
                    for(i=0;i<=strlen(filename);i++)
                    {
                        c[i]=toupper(filename[i]);
                    }
                    for(;i<=10;i++)
                        c[i]=' ';

                    c[11] = 0x01;

                    c[26] = (clusterno[0] &  0x00ff);
                    c[27] = ((clusterno[0] & 0xff00)>>8);

                    c[28] = (size &  0x000000ff);
                    c[29] = ((size & 0x0000ff00)>>8);
                    c[30] = ((size& 0x00ff0000)>>16);
                    c[31] = ((size& 0xff000000)>>24);

                    if(lseek(fd,offset,SEEK_SET)<0)
                        err(1, "lseek fd_cf failed");
                    if(write(fd,&c,DIR_ENTRY_SIZE)<0)
                        err(1, "write failed");




                    free(pentry);
                    WriteFat();

                    return 1;
                }

            }
        }
    }
    else
    {
        printf("This filename is exist\n");
        free(pentry);
        return -1;
    }
    return 1;

}

void help(void)
{
    fprintf(stderr, "available commands\n"
           "\tls\t\t\tlist all files\n"
           "\tcd <dir>\t\tchange direcotry\n"
           "\tcf <filename> <size>\tcreate a file\n"
           "\tdf <file>\t\tdelete a file\n"
           "\texit\t\t\texit this system\n");
}

void usage(void)
{
    exit(2);
}

int main(int argc, char **argv)
{
    char input[8];
    int size=0;
    char name[12];
    if (argc<2)
        usage();
    if((fd = open(argv[1],O_RDWR))<0)
        err(1, "open failed");//以可读写方式打开文件
    ScanBootSector();
    ReadFat();
    help();
    for (;;) {
        fprintf(stderr, "> ");
        if (scanf("%8s", input) < 1)
            break;

        if (strcmp(input, "exit") == 0)
            break;
        else if (strcmp(input, "ls") == 0)
            fd_ls();
        else if(strcmp(input, "cd") == 0)
        {
            scanf("%s", name);
            fd_cd(name);
        }
        else if(strcmp(input, "df") == 0)
        {
            scanf("%s", name);
            fd_df(name);
        }
        else if(strcmp(input, "cf") == 0)
        {
            scanf("%s", name);
            scanf("%s", input);
            size = atoi(input);
            fd_cf(name,size);
        }
        else
            help();
    }

    return 0;
}
