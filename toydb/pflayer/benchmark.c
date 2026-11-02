#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pf.h"
extern int PF_CreateFile(), PF_AllocPage(), PF_CloseFile();
extern int PF_GetThisPage(), PF_UnfixPage();
static void check(int result) {
    if (result < 0) { PF_PrintError("benchmark"); exit(1); }
}
static double seconds(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
/* Each invocation runs in its own scratch directory and process. */
int main(int argc, char **argv) {
    int capacity, pages, operations, writes, policy, sequential;
    int fd, i, page, do_write; char *data;
    unsigned long state; double start, elapsed; PF_Stats stats;
    if (argc != 8) return 2;
    capacity=atoi(argv[1]); pages=atoi(argv[2]); operations=atoi(argv[3]);
    writes=atoi(argv[4]); policy=atoi(argv[5]); sequential=atoi(argv[6]);
    state=strtoul(argv[7], NULL, 10);
    if(capacity<1 || pages<1 || operations<1 || writes<0 || writes>100) return 2;
    check(PF_InitWithBufferSize(capacity));
    check(PF_CreateFile("workload.pf"));
    fd=PF_OpenFileWithPolicy("workload.pf",policy); check(fd);
    for(i=0;i<pages;i++) {
        check(PF_AllocPage(fd,&page,&data));
        memset(data,0,PF_PAGE_SIZE);
        check(PF_UnfixPage(fd,page,TRUE));
    }
    check(PF_CloseFile(fd));
    fd=PF_OpenFileWithPolicy("workload.pf",policy); check(fd);
    PF_ResetStats(); start=seconds();
    for(i=0;i<operations;i++) {
        state=(1664525UL*state+1013904223UL)&0xffffffffUL;
        page=sequential ? i%pages : (int)((state>>8)%pages);
        /* Exactly floor(operations * writes / 100) writes, interspersed. */
        do_write=((long long)(i+1)*writes/100 != (long long)i*writes/100);
        check(PF_GetThisPage(fd,page,&data));
        if(do_write) { data[0]=(char)((unsigned char)data[0]^1); check(PF_MarkDirty(fd,page)); }
        check(PF_UnfixPage(fd,page,FALSE));
    }
    check(PF_CloseFile(fd)); /* Include the final dirty flush in I/O and time. */
    elapsed=seconds()-start; stats=PF_GetStats();
    printf("%llu,%llu,%llu,%llu,%llu,%llu,%.9f\n",
        stats.logical_reads,stats.logical_writes,stats.physical_reads,
        stats.physical_writes,stats.buffer_hits,stats.buffer_misses,elapsed);
    return 0;
}
