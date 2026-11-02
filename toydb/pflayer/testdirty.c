/* Run in a fresh directory: testdirty lru|mru. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pf.h"
#include "pftypes.h"
extern int PF_CreateFile(), PF_AllocPage(), PF_GetThisPage();
extern int PF_UnfixPage(), PF_CloseFile();
int main(int argc, char **argv)
{
    int fd, page, next, policy;
    char *data;
    PFbpage *frame;
    assert(argc == 2);
    assert(!strcmp(argv[1], "lru") || !strcmp(argv[1], "mru"));
    policy = !strcmp(argv[1], "mru") ? PF_POLICY_MRU : PF_POLICY_LRU;
    assert(PF_InitWithBufferSize(1) == PFE_OK);
    assert(PF_MarkDirty(-1, 0) == PFE_FD);
    assert(PF_CreateFile("dirty.pf") == PFE_OK);
    fd = PF_OpenFileWithPolicy("dirty.pf", policy);
    assert(fd >= 0);
    assert(PF_MarkDirty(fd, 0) == PFE_INVALIDPAGE);
    assert(PF_AllocPage(fd, &page, &data) == PFE_OK);
    data[0] = 10;
    assert(PF_UnfixPage(fd, page, TRUE) == PFE_OK);
    /* Reopen to test an initially clean page, not a new dirty allocation. */
    assert(PF_CloseFile(fd) == PFE_OK);
    fd = PF_OpenFileWithPolicy("dirty.pf", policy);
    assert(PF_GetThisPage(fd, page, &data) == PFE_OK);
    frame = PFhashFind(fd, page);
    assert(frame != NULL && !frame->dirty);
    data[0] = 42;
    assert(PF_MarkDirty(fd, page) == PFE_OK);
    assert(PF_MarkDirty(fd, page) == PFE_OK);
    assert(frame->dirty && frame->fixed);
    assert(PF_CloseFile(fd) == PFE_PAGEFIXED);
    assert(PF_UnfixPage(fd, page, FALSE) == PFE_OK);
    assert(frame->dirty); /* FALSE cannot erase a previously reported update. */
    assert(PF_MarkDirty(fd, page) == PFE_PAGEUNFIXED);
    assert(PF_AllocPage(fd, &next, &data) == PFE_OK);
    assert(PF_UnfixPage(fd, next, FALSE) == PFE_OK);
    assert(PF_MarkDirty(fd, page) == PFE_PAGENOTINBUF);
    /* Reload the evicted page to verify dirty writeback. */
    assert(PF_GetThisPage(fd, page, &data) == PFE_OK);
    assert(data[0] == 42);
    data[0] = 77;
    assert(PF_MarkDirty(fd, page) == PFE_OK);
    assert(PF_UnfixPage(fd, page, FALSE) == PFE_OK);
    assert(PF_CloseFile(fd) == PFE_OK);
    assert(PF_MarkDirty(fd, page) == PFE_FD);
    fd = PF_OpenFileWithPolicy("dirty.pf", policy);
    assert(PF_GetThisPage(fd, page, &data) == PFE_OK);
    assert(data[0] == 77);
    assert(PF_UnfixPage(fd, page, FALSE) == PFE_OK);
    assert(PF_CloseFile(fd) == PFE_OK);
    printf("%s explicit dirty marking: passed\n", argv[1]);
    return 0;
}
