#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <err.h>
#include <time.h>

struct {
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
} hdr;

enum {
    READONLY  = 1,
    HIDDEN    = 2,
    ISSYSTEM  = 4,
    ISVOLNAME = 8,
    ISDIR     = 16,
    ARCHIVED  = 32,
};

struct dirent {
    uint8_t filename[11];
    uint8_t attrib;
    uint8_t reserved1;
    uint8_t ctime_milli;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t reserved2;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t start_cluster;
    uint32_t filesize;
};

void setfat(int cl, int link);
void walkdir(struct dirent *ent, void (*walkfn)(struct dirent *));

uint8_t *fs;
int fs_size;
uint16_t *fat;
int n_clusters;

struct dirent *rootdir;
struct dirent *curdir;
int rootdir_size;
uint8_t *data;

int clsize;

void init(void)
{
    uint8_t *p = fs+3;

    int i;
    for (i=0; i<8; i++)
        hdr.Oem_name[i] = *p++;
    hdr.Oem_name[i] = 0;

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

    fat = (uint16_t *)(fs + hdr.BytesPerSector * hdr.ReservedSectors);
    int fat_size = hdr.BytesPerSector * hdr.SectorsPerFAT;
    int rootdir_offset = hdr.BytesPerSector + hdr.FATs * fat_size;
    rootdir = (struct dirent *)(fs + rootdir_offset);
    rootdir_size = hdr.RootDirEntries * sizeof *rootdir;
    data = (uint8_t *) rootdir + rootdir_size;

    clsize = hdr.SectorsPerCluster * hdr.BytesPerSector;
    n_clusters = (fs+fs_size-data)/clsize;

    printf("%.8s\n", fs+0x36);

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
}

/* 10 chars */
void format_date(char *s, uint16_t date)
{
    int year = 1980 + (date >> 9);
    int month = date >> 5 & 15;
    int day = date & 31;
    sprintf(s, "%d-%02d-%02d", year, month, day);
}

/* 8 chars */
void format_time(char *s, uint16_t time)
{
    int hour = time >> 11;
    int minute = time >> 5 & 63;
    int second = (time & 31)<<1;
    sprintf(s, "%02d:%02d:%02d", hour, minute, second);
}

/* 19 chars: yyyy-mm-dd hh:mm:ss */
void format_datetime(char *s, uint16_t date, uint16_t time)
{
    format_date(s, date);
    s[10] = ' ';
    format_time(s+11, time);
}

void format_attrib(char *s, uint8_t attrib)
{
    static char *letters = "rhsvda";
    int i;
    for (i=0; i<6; i++) {
        s[i] = attrib&1<<i ? letters[i] : '-';
    }
    s[i] = 0;
}

void print_entry(struct dirent *ent)
{
    if (ent->filename[0] == 0xe5 || ent->attrib == 15)
        return;
    //char ctime[20], mtime[20], atime[12]
    char ctime[20];
    char attrib[8];
    format_datetime(ctime, ent->cdate, ent->ctime);
    //format_datetime(mtime, ent->mdate, ent->mtime);
    //format_date(atime, ent->adate);
    format_attrib(attrib, ent->attrib);
    printf("%.8s %.3s %s %5d %4hx %s %s\n",
           ent->filename, ent->filename+8,
        ent->attrib & ISDIR ? "DIR" : "", ent->filesize, ent->start_cluster, attrib,
        ctime);
}

int clvalid(int cl)
{
    return cl>2 && cl<0xfff0;
}

int cleof(int cl)
{
    return cl>=0xfff8;
}

void *cldata(int cl)
{
    assert(clvalid(cl));
    return data + (cl-2)*clsize;
}

void fd_ls(struct dirent *ent)
{
    walkdir(ent, print_entry);
}

int is_valid_filename1(char *s, int n)
{
    int i;
    for (i=0; i<n; i++) {
        char c = s[i];
        if (c<=' ')
            return 0;
        switch (c) {
        case '"': case '*': case '+': case ',': case '.': case '/':
        case ':': case ';': case '<': case '=': case '>': case '?':
        case '[': case '\\': case ']': case '|':
            return 0;
        }
    }
    return 1;
}

int is_valid_filename(char *s)
{
    int len = strlen(s);
    char *pdot = strchr(s, '.');
    if (pdot) {
        int idot = pdot-s;
        int len_ext = len-(idot+1);
        return idot<=8 && len_ext<=3 && is_valid_filename1(s, idot) &&
            is_valid_filename1(s+(idot+1), len_ext);
    }
    return len<=8 && is_valid_filename1(s, len);
}

void encode_filename(char *s, char *name)
{
    int len = strlen(name);
    int i;
    char *pdot = strchr(name, '.');
    if (pdot) {
        int idot = pdot-name;
        int len_ext = len-(idot+1);
        for (i=0; i<idot; i++)
            s[i] = toupper(name[i]);
        for (; i<8; i++)
            s[i] = ' ';
        for (i=0; i<len_ext; i++)
            s[8+i] = pdot[1+i];
        for (; i<3; i++)
            s[8+i] = ' ';
    } else {
        for (i=0; i<len; i++)
            s[i] = toupper(name[i]);
        for (; i<8; i++)
            s[i] = ' ';
        for (i=0; i<3; i++)
            s[8+i] = ' ';
    }
}

void decode_filename(char *s, char *fs_filename)
{
    int i;
    for (i=0; i<8; i++)
        if (fs_filename[i] == ' ')
            break;
    int baselen = i;
    for (i=0; i<3; i++)
        if (fs_filename[8+i] == ' ')
            break;
    int extlen = i;
    if (extlen)
        sprintf(s, "%.*s.%.*s", baselen, fs_filename, extlen, fs_filename+8);
    else
        sprintf(s, "%.*s", baselen, fs_filename);
}

struct dirent *
find_entry1(struct dirent *start, struct dirent *end, char *filename)
{
    struct dirent *e;
    for (e=start; e<end; e++) {
        if (!e->filename[0]) /* no more entries */
            return 0;
        if (e->filename[0] == 0xe5) /* free entry */
            continue;
        if (e->attrib == 15) /* LFN */
            continue;
        char fmt_filename[13];
        decode_filename(fmt_filename, e->filename);
        if (!strcmp(fmt_filename, filename))
            return e;
    }
    return e;
}

struct dirent *
find_empty_entry1(struct dirent *start, struct dirent *end)
{
    struct dirent *e;
    for (e=start; e<end; e++)
        if (!e->filename[0] || e->filename[0] == 0xe5)
            return e;
    return e;
}

#define ROOTDIR_END ((struct dirent *) data)

/* returns 0 if not found */
struct dirent *
find_entry(struct dirent *ent, struct dirent * (*findfn)(), void *arg)
{
    struct dirent *e;
    if (ent) {
        assert(ent->attrib & ISDIR);
        struct dirent *dir, *end;
        int cl = ent->start_cluster;
        while (clvalid(cl)) {
            dir = cldata(cl);
            end = (struct dirent *)((char *) dir + clsize);
            e = findfn(dir, end, arg);
            if (!e)
                return 0;
            if (e<end)
                return e;
            cl = fat[cl];
        }
        return 0;
    }
    e = findfn(rootdir, ROOTDIR_END, arg);
    if (e == ROOTDIR_END)
        e = 0;
    return e;
}

struct dirent *find_entry_with_name(struct dirent *ent, char *name)
{
    return find_entry(ent, find_entry1, name);
}

struct dirent *find_empty_entry(struct dirent *ent)
{
    return find_entry(ent, find_empty_entry1, 0);
}

int check_filename(char *name)
{
    int ret = is_valid_filename(name);
    if (!ret)
        printf("invalid filename: %s\n", name);
    return ret;
}

/* cd one level */
void fd_cd1(char *arg)
{
    if (!strcmp(arg, "..")) {
        if (curdir) {
            struct dirent *dir = cldata(curdir->start_cluster);
            curdir = dir[1].start_cluster ? &dir[1] : 0;
        }
    } else {
        if (check_filename(arg)) {
            struct dirent *ent = find_entry_with_name(curdir, arg);
            if (ent) {
                if (ent->attrib & ISDIR)
                    curdir = ent;
                else
                    printf("not a directory: %s\n", arg);
            } else {
                printf("not found: %s\n", arg);
            }
        }
    }
}

void erase(struct dirent *ent)
{
    int cl = ent->start_cluster;
    memset(ent, 0, sizeof *ent);
    ent->filename[0] = 0xe5;
    if (cl) {
        do {
            int link = fat[cl];
            setfat(cl, 0);
            cl = link;
        } while (clvalid(cl));
    }
}

void fd_rm(struct dirent *ent, char *name)
{
    struct dirent *e = find_entry_with_name(ent, name);
    if (e) {
        if (e->attrib & ISDIR)
            walkdir(e, erase);
        erase(e);
    } else {
        printf("not found: %s\n", name);
    }
}

void walkdir(struct dirent *ent, void (*walkfn)(struct dirent *))
{
    if (ent) {
        assert(ent->attrib & ISDIR);
        int cl = ent->start_cluster;
        while (clvalid(cl)) {
            struct dirent *e = cldata(cl);
            struct dirent *end = (struct dirent *)((char *) e + clsize);
            while (e<end) {
                if (!e->filename[0])
                    return;
                walkfn(e++);
            }
            cl = fat[cl];
        }
    } else {
        struct dirent *e = rootdir;
        while (e < ROOTDIR_END) {
            if (!e->filename[0])
                return;
            walkfn(e++);
        }
    }
}

void fd_cat(struct dirent *ent, char *name)
{
    struct dirent *e = find_entry_with_name(ent, name);
    if (e) {
        if (e->attrib & ISDIR) {
            printf("is a directory: %s\n", name);
        } else {
            int cl = e->start_cluster;
            int rem = e->filesize;
            for (;;) {
                void *cld = cldata(cl);
                if (rem <= clsize) {
                    write(1, cld, rem);
                    break;
                }
                write(1, cld, clsize);
                rem -= clsize;
                cl = fat[cl];
            }
        }
    } else {
        printf("not found: %s\n", name);
    }
}

int alloc(void)
{
    int i;
    for (i=3; i<n_clusters; i++)
        if (!fat[i])
            return i;
    return 0;
}

void setfat(int cl, int link)
{
    assert(clvalid(cl) && cl < n_clusters);
    fat[cl] = link;
}

void zero_file(int cl)
{
    if (cl) do {
        memset(cldata(cl), 0, clsize);
        cl = fat[cl];
    } while (clvalid(cl));
}

uint16_t fatdate(struct tm *tm)
{
    return tm->tm_year-80<<9 | tm->tm_mon+1<<5 | tm->tm_mday;
}

uint16_t fattime(struct tm *tm)
{
    return tm->tm_hour<<11 | tm->tm_min<<5 | tm->tm_sec>>1;
}

void init_time(struct dirent *e, struct tm *tm)
{
    e->adate = e->mdate = e->cdate = fatdate(tm);
    e->mtime = e->ctime = fattime(tm);
}

/* won't check for duplicate file name */
/* size<0 = mkdir */
void fd_cf(struct dirent *ent, char *filename, int size)
{
    struct dirent *e = find_empty_entry(ent);
    if (!e) {

        puts("directory is full");
        return;
    }
    encode_filename(e->filename, filename);
    struct dirent *sub = 0;
    if (size<0) {
        int cl = alloc();
        if (!cl) {
            puts("filesystem is full");
            return;
        }
        setfat(cl, 0xffff);
        sub = cldata(cl);
        /* create . and .. entries */
        strncpy(sub[0].filename, ".          ", 11);
        sub[0].attrib = ISDIR;
        sub[0].start_cluster = cl; /* same as e */
        strncpy(sub[1].filename, "..         ", 11);
        sub[1].attrib = ISDIR;
        sub[1].start_cluster = ent ? ent->start_cluster : 0;
        memset(&sub[2], 0, sizeof *sub);
        e->start_cluster = cl;
        e->attrib = ISDIR;
        size = 0;
    } else {
        int ncl = (size+(clsize-1))/clsize;
        int i;
        int link = 0xffff;
        int cl = 0;
        for (i=0; i<ncl; i++) {
            int tmp = alloc();
            if (!tmp) {
                puts("filesystem is full");
                break;
            }
            cl = tmp;
            setfat(cl, link);
            link = cl;
        }
        e->start_cluster = cl;
        zero_file(cl);
        if (i<ncl)
            size = clsize*i;
    }
    e->filesize = size;
    /* date and time */
    time_t now = time(0);
    struct tm *tm = gmtime(&now);
    init_time(e, tm);
    if (sub) {
        init_time(&sub[0], tm);
        init_time(&sub[1], tm);
    }
}

void help(void)
{
    fprintf(stderr, "available commands\n"
           "\tls\t\t\tlist files\n"
           "\tcd <dir>\t\tchange directory\n"
           "\tcf <file> <size>\tcreate a file\n"
           "\trm <file>\t\tdelete file or directory\n"
           "\tmkdir <dir>\t\tcreate directory\n"
           "\tcat <file>\t\tshow file contents\n"
           "\texit\t\t\texit program\n");
}

void usage(void)
{
    exit(2);
}

int main(int argc, char **argv)
{
    char input[8];
    char name[12];
    if (argc<2)
        usage();
    int fd;
    if((fd = open(argv[1],O_RDWR))<0)
        err(1, argv[1]);

    struct stat st;
    fstat(fd, &st);
    fs = mmap(0, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    fs_size = st.st_size;
    if (fs == (void *) -1)
        err(1, "mmap");
    init();

    help();
    for (;;) {
        fprintf(stderr, "> ");
        if (scanf("%8s", input) < 1)
            break;
        if (strcmp(input, "exit") == 0)
            break;
        if (strcmp(input, "ls") == 0) {
            fd_ls(curdir);
        } else if(strcmp(input, "cd") == 0) {
            if (scanf("%12s", name) == 1)
                fd_cd1(name);
        } else if(strcmp(input, "rm") == 0) {
            if (scanf("%12s", name) == 1)
                if (check_filename(name))
                    fd_rm(curdir, name);
        } else if(strcmp(input, "cf") == 0) {
            int size;
            if (scanf("%12s", name) == 1 && scanf("%d", &size) == 1) {
                if (check_filename(name)) {
                    if (size >= 0) {
                        fd_cf(curdir, name, size);
                    } else {
                        printf("invalid size: %d\n", size);
                    }
                }
            }
        } else if (!strcmp(input, "mkdir")) {
            if (scanf("%12s", name) == 1) {
                if (check_filename(name))
                    fd_cf(curdir, name, -1);
            }
        } else if (!strcmp(input, "cat")) {
            if (scanf("%12s", name) == 1) {
                if (check_filename(name)) {
                    fd_cat(curdir, name);
                }
            }
        } else {
            help();
        }
    }
    return 0;
}
