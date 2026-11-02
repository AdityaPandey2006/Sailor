#include <assert.h>
#include <stdio.h>
#include "pf.h"
extern int PF_CreateFile(), PF_AllocPage(), PF_CloseFile();
extern int PF_GetThisPage(), PF_UnfixPage();
int main(void) {
    int fd, p; char *data; PF_Stats s;
    assert(PF_InitWithBufferSize(1)==PFE_OK);
    assert(PF_CreateFile("stats.pf")==PFE_OK);
    fd=PF_OpenFileWithPolicy("stats.pf",PF_POLICY_LRU); assert(fd>=0);
    assert(PF_AllocPage(fd,&p,&data)==PFE_OK);
    assert(PF_UnfixPage(fd,p,TRUE)==PFE_OK);
    assert(PF_CloseFile(fd)==PFE_OK);
    fd=PF_OpenFileWithPolicy("stats.pf",PF_POLICY_LRU); assert(fd>=0);
    PF_ResetStats();
    assert(PF_GetThisPage(fd,p,&data)==PFE_OK); /* miss */
    assert(PF_MarkDirty(fd,p)==PFE_OK);
    assert(PF_UnfixPage(fd,p,FALSE)==PFE_OK);
    assert(PF_GetThisPage(fd,p,&data)==PFE_OK); /* hit */
    assert(PF_UnfixPage(fd,p,TRUE)==PFE_OK);
    assert(PF_GetThisPage(-1,p,&data)==PFE_FD);
    assert(PF_CloseFile(fd)==PFE_OK);
    s=PF_GetStats();
    assert(s.logical_reads==2 && s.logical_writes==2);
    assert(s.buffer_hits==1 && s.buffer_misses==1);
    assert(s.physical_reads==1 && s.physical_writes==1);
    PF_ResetStats(); s=PF_GetStats();
    assert(!s.logical_reads && !s.logical_writes && !s.physical_reads &&
           !s.physical_writes && !s.buffer_hits && !s.buffer_misses);
    puts("Statistics: passed"); return 0;
}
