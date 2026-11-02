/* Run in a scratch directory: testcapacity [positive capacity]. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "pf.h"
extern int PF_CreateFile(), PF_OpenFile(), PF_AllocPage();
extern int PF_UnfixPage(), PF_CloseFile(), PF_GetThisPage();
int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 2;
    int fd, i, page, extra;
    char *data;
    assert(n > 0);
    assert(PF_InitWithBufferSize(0) == PFE_INVALIDBUFSIZE);
    assert(PF_InitWithBufferSize(-1) == PFE_INVALIDBUFSIZE);
    assert(PF_InitWithBufferSize(n) == PFE_OK);
    assert(PF_OpenFileWithPolicy("missing.pf", 99) == PFE_INVALIDPOLICY);
    assert(PF_CreateFile("capacity.pf") == PFE_OK);
    fd = PF_OpenFileWithPolicy("capacity.pf", PF_POLICY_MRU);
    assert(fd >= 0);
    assert(PF_GetFilePolicy(fd) == PF_POLICY_MRU);
    assert(PF_GetFilePolicy(-1) == PFE_FD);
    assert(PF_CloseFile(fd) == PFE_OK);
    assert(PF_GetFilePolicy(fd) == PFE_FD);
    fd = PF_OpenFile("capacity.pf");
    assert(fd >= 0);
    assert(PF_GetFilePolicy(fd) == PF_POLICY_LRU);
    assert(PF_InitWithBufferSize(n + 1) == PFE_FILEOPEN);
    for (i = 0; i < n; i++) {
        assert(PF_AllocPage(fd, &page, &data) == PFE_OK);
        assert(page == i);
        data[0] = (char)(i % 100);
    }
    /* Every configured frame is pinned: another allocation must fail. */
    assert(PF_AllocPage(fd, &extra, &data) == PFE_NOBUF);
    for (i = 0; i < n; i++)
        assert(PF_UnfixPage(fd, i, TRUE) == PFE_OK);
    /* Unpinning allows eviction and dirty writeback. */
    assert(PF_AllocPage(fd, &extra, &data) == PFE_OK);
    assert(PF_UnfixPage(fd, extra, FALSE) == PFE_OK);
    assert(PF_CloseFile(fd) == PFE_OK);
    assert(PF_InitWithBufferSize(n + 1) == PFE_BUFINIT);
    fd = PF_OpenFile("capacity.pf");
    assert(fd >= 0);
    for (i = 0; i < n; i++) {
        assert(PF_GetThisPage(fd, i, &data) == PFE_OK);
        assert(data[0] == (char)(i % 100));
        assert(PF_UnfixPage(fd, i, FALSE) == PFE_OK);
    }
    assert(PF_CloseFile(fd) == PFE_OK);
    printf("Capacity %d: passed\n", n);
    return 0;
}
