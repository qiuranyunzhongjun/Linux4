﻿#include<stddef.h>
/*
 * 下面这些暴力算的虽然正确，但其实不合法。
 * 我们从BootSector中可以读出整个FAT的参数。
 * 这些参数会在ScanBootSector()的时候保存在BootDescriptor_t中
 */
#define DIR_ENTRY_SIZE 32
#define SECTOR_SIZE 512
#define CLUSTER_SIZE 512*4                         
#define FAT_ONE_OFFSET 512                       
#define FAT_TWO_OFFSET 512+250*512                       
#define ROOTDIR_OFFSET 512+250*512+250*512+512                     
#define DATA_OFFSET 512+250*512+250*512+512*32        

           

/*属性位掩码*/
#define ATTR_READONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VLABEL 0x08
#define ATTR_SUBDIR 0x10
#define ATTR_ARCHIVE 0x20

/*时间掩码 5：6：5 */
#define MASK_HOUR 0xf800 
#define MASK_MIN 0x07e0
#define MASK_SEC 0x001f

/*日期掩码*/
#define MASK_YEAR 0xfe00
#define MASK_MONTH 0x01e0
#define MASK_DAY 0x001f

struct BootDescriptor_t{
	unsigned char Oem_name[9]; /*0x03-0x0a*/
	int BytesPerSector;        /*0x0b-0x0c*/
	int SectorsPerCluster;     /*0x0d*/
	int ReservedSectors;       /*0x0e-0x0f*/
	int FATs;                  /*0x10*/
	int RootDirEntries;        /*0x11-0x12*/
	int LogicSectors;          /*0x13-0x14*/
	int MediaType;             /*0x15*/
	int SectorsPerFAT;         /*0x16-0x17*/
	int SectorsPerTrack;       /*0x18-0x19*/
	int Heads;                 /*0x1a-0x1b*/
	int HiddenSectors;         /*0x1c-0x1d*/
};

/*目录项,一共32字节*/
struct Entry{
	unsigned char short_name[12];   /*字节0-10，11字节的短文件名*/
	unsigned char long_name[27];    /*未使用，26字节的长文件名*/
	unsigned short year,month,day;  /*22-23字节*/
	unsigned short hour,min,sec;    /*24-25字节*/
	unsigned short FirstCluster;    /*26-27字节*/
	unsigned int size;              /*28-31字节*/
	/*属性值                        11字节
	*7  6  5  4  3  2  1  0
	*N  N  A  D  V  S  H  R         N未使用
	*/

        //这是某种C语言黑科技，表示定义的变量按照unsigned char解析，但是所占空间只分配一个bit。。。并不知道上一任助教或者老师从哪里找来的这种黑科技（亲爱的助教您连这都不知道实在是太水了）
	unsigned char readonly:1;
	unsigned char hidden:1;
	unsigned char system:1;
	unsigned char vlabel:1;
	unsigned char subdir:1;
	unsigned char archive:1;
};

//下面这些函数的作用我写到.c文件中了。
int fd_ls();
int fd_cd(char *dir);
int fd_df(char *file_name);
int fd_cf(char *file_name,int size);

void findDate(unsigned short *year,
			  unsigned short *month,
			  unsigned short *day,
			  unsigned char info[2]);

void findTime(unsigned short *hour,
			  unsigned short *min,
			  unsigned short *sec,
			  unsigned char info[2]);
int ReadFat();
int WriteFat();
void ScanBootSector();
void ScanRootEntry();
int ScanEntry(char *entryname,struct Entry *pentry,int mode);
int GetEntry(struct Entry *entry);
void FileNameFormat(unsigned char *name);
unsigned short GetFatCluster(unsigned short prev);
void ClearFatCluster(unsigned short cluster);

int fd;
struct BootDescriptor_t bdptor;
struct Entry *curdir = NULL;//当前所在的目录，默认NULL表示位于根目录
int dirno = 0;/*代表目录的层数*/
struct Entry* fatherdir[10];

unsigned char fatbuf[512*250];  
