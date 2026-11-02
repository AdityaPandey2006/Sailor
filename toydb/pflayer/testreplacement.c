/* Run each policy in a fresh scratch directory. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pf.h"
#include "pftypes.h"
extern int PF_CreateFile(), PF_AllocPage(), PF_UnfixPage();
extern int PF_GetThisPage(), PF_CloseFile();
static void add(int fd, int expected, char value) {
    int page; char *data;
    assert(PF_AllocPage(fd, &page, &data) == PFE_OK);
    assert(page == expected); data[0] = value;
    assert(PF_UnfixPage(fd, page, TRUE) == PFE_OK);
}
int main(int argc, char **argv) {
    int policy, other, a, b, i, page; char *data;
    assert(argc == 2);
    assert(!strcmp(argv[1], "lru") || !strcmp(argv[1], "mru"));
    policy = !strcmp(argv[1], "mru") ? PF_POLICY_MRU : PF_POLICY_LRU;
    other = policy == PF_POLICY_MRU ? PF_POLICY_LRU : PF_POLICY_MRU;
    assert(PF_InitWithBufferSize(3) == PFE_OK);
    assert(PF_CreateFile("a.pf") == PFE_OK);
    assert(PF_CreateFile("b.pf") == PFE_OK);
    a = PF_OpenFileWithPolicy("a.pf", other);
    b = PF_OpenFileWithPolicy("b.pf", policy);
    assert(a >= 0 && b >= 0);
    for (i = 0; i < 3; i++) add(a, i, (char)(10+i));
    /* Hit page 0: newest to oldest is now 0, 2, 1. */
    assert(PF_GetThisPage(a, 0, &data) == PFE_OK);
    assert(PF_UnfixPage(a, 0, FALSE) == PFE_OK);
    add(b, 0, 20);
    /* B must evict a page of A using B's policy, not A's. */
    assert(PFhashFind(a, policy == PF_POLICY_MRU ? 0 : 1) == NULL);
    assert(PFhashFind(a, policy == PF_POLICY_MRU ? 1 : 0) != NULL);
    assert(PFhashFind(a, 2) != NULL);
    assert(PF_CloseFile(a) == PFE_OK);
    assert(PF_CloseFile(b) == PFE_OK);
    a = PF_OpenFileWithPolicy("a.pf", policy);
    for (i = 0; i < 3; i++) {
        assert(PF_GetThisPage(a, i, &data) == PFE_OK);
        assert(data[0] == 10+i);
        /* Leave all three pages pinned. */
    }
    assert(PF_AllocPage(a, &page, &data) == PFE_NOBUF);
    /* Only page 1 is eligible, regardless of replacement direction. */
    assert(PF_UnfixPage(a, 1, FALSE) == PFE_OK);
    assert(PF_AllocPage(a, &page, &data) == PFE_OK);
    assert(PFhashFind(a, 1) == NULL);
    assert(PFhashFind(a, 0) != NULL && PFhashFind(a, 2) != NULL);
    assert(PF_UnfixPage(a, page, FALSE) == PFE_OK);
    assert(PF_UnfixPage(a, 0, FALSE) == PFE_OK);
    assert(PF_UnfixPage(a, 2, FALSE) == PFE_OK);
    assert(PF_CloseFile(a) == PFE_OK);
    printf("%s global replacement: passed\n", argv[1]);
    return 0;
}
