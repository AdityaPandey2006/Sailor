/* testslotted.c: unit tests for the slotted-page layer.
 *
 * usage: testslotted <buffer_pages> <cached|scan>
 *
 * Run it in an empty scratch directory (run_tests.py does this).  Running
 * with a 1-page buffer pool proves that no call leaves a page fixed: any
 * leak would make the next fetch fail with PFE_NOBUF.
 *
 * The page arithmetic below uses 100-byte records, each costing 104 bytes
 * (record + 4-byte slot), so a page holds (4096 - 8) / 104 = 39 of them and
 * keeps 4096 - 8 - 39*104 = 32 bytes free. */
#include <limits.h>
#include "slotted.h"
#include "testutil.h"

extern int PF_OpenFile(char *fname);

static int search_mode;

static int rid(int page, int slot) { return REC_MakeRid(page, slot); }

static void sp_collect(int fd, Shadow *out)
{
    SP_Scan sc;
    char buf[PF_PAGE_SIZE];
    int r, len, err;
    shadow_clear(out);
    CHECK_EQ(SP_ScanOpen(fd, &sc), RECE_OK);
    while ((err = SP_ScanNext(&sc, &r, buf, sizeof buf, &len)) == RECE_OK)
        shadow_add(out, r, buf, len);
    CHECK_EQ(err, RECE_EOF);
    CHECK_EQ(SP_ScanClose(&sc), RECE_OK);
}

/* Scan, point lookups and space accounting must all agree with the shadow. */
static void verify(int fd, const Shadow *s)
{
    Shadow scanned = {0};
    REC_SpaceStats st;
    char buf[PF_PAGE_SIZE];
    int i, len, page;

    sp_collect(fd, &scanned);
    shadow_check_scan(s, &scanned);
    shadow_free(&scanned);
    for (i = 0; i < s->n; i++) {
        CHECK_EQ(SP_GetRec(fd, s->rid[i], buf, sizeof buf, &len), RECE_OK);
        CHECK_EQ(len, s->len[i]);
        CHECK(memcmp(buf, s->data[i], len) == 0);
    }
    CHECK_EQ(SP_GetSpaceStats(fd, &st), RECE_OK);
    check_space_invariant(&st, s);
    CHECK_EQ(st.paddingBytes, 0);
    CHECK_EQ(st.numPages, SP_NumPages(fd));
    for (page = 0; page < st.numPages; page++) {
        SP_PageInfo info;
        CHECK_EQ(SP_GetPageInfo(fd, page, &info), RECE_OK);
        CHECK(info.freeStart <= info.freeEnd);
        CHECK(info.liveCount <= info.numSlots);
    }
}

static void insert_ok(int fd, Shadow *s, int len, int tag, int expectRid)
{
    char buf[PF_PAGE_SIZE];
    int r;
    make_record(buf, len, tag);
    CHECK_EQ(SP_InsertRec(fd, buf, len, &r), RECE_OK);
    if (expectRid >= 0)
        CHECK_EQ(r, expectRid);
    shadow_add(s, r, buf, len);
}

static void delete_ok(int fd, Shadow *s, int r)
{
    int i = shadow_find(s, r);
    CHECK(i >= 0);
    CHECK_EQ(SP_DeleteRec(fd, r), RECE_OK);
    shadow_remove(s, i);
}

static int page_free(int fd, int page)
{
    SP_PageInfo info;
    CHECK_EQ(SP_GetPageInfo(fd, page, &info), RECE_OK);
    return info.freeEnd - info.freeStart;
}

static void test_rid_encoding(void)
{
    int p, s;
    CHECK_EQ(REC_MakeRid(0, 0), 0);
    CHECK_EQ(REC_MakeRid(1, 2), 65538);
    CHECK_EQ(REC_MakeRid(REC_MAX_PAGE, REC_MAX_SLOT), INT_MAX);
    CHECK_EQ(REC_MakeRid(REC_MAX_PAGE + 1, 0), RECE_INVALIDRID);
    CHECK_EQ(REC_MakeRid(-1, 0), RECE_INVALIDRID);
    CHECK_EQ(REC_MakeRid(0, REC_MAX_SLOT + 1), RECE_INVALIDRID);
    CHECK_EQ(REC_SplitRid(INT_MAX, &p, &s), RECE_OK);
    CHECK_EQ(p, REC_MAX_PAGE);
    CHECK_EQ(s, REC_MAX_SLOT);
    CHECK_EQ(REC_SplitRid(65538, &p, &s), RECE_OK);
    CHECK_EQ(p, 1);
    CHECK_EQ(s, 2);
    CHECK_EQ(REC_SplitRid(-5, &p, &s), RECE_INVALIDRID);
    printf("  rid encoding ok\n");
}

static void test_basic_and_errors(void)
{
    char buf[PF_PAGE_SIZE + 16];
    int fd, r, len;

    CHECK_EQ(SP_CreateFile("basic"), PFE_OK);
    CHECK_EQ(SP_OpenFile("basic", PF_POLICY_LRU, 7), RECE_INVALIDARG);
    CHECK((fd = SP_OpenFile("basic", PF_POLICY_LRU, search_mode)) >= 0);
    CHECK_EQ(SP_NumPages(fd), 0);

    CHECK_EQ(SP_InsertRec(fd, "hello", 5, &r), RECE_OK);
    CHECK_EQ(r, rid(0, 0));
    CHECK_EQ(SP_GetRec(fd, r, buf, sizeof buf, &len), RECE_OK);
    CHECK(len == 5 && memcmp(buf, "hello", 5) == 0);
    CHECK_EQ(SP_GetRec(fd, r, buf, 4, &len), RECE_BUFSMALL);
    CHECK_EQ(len, 5);

    CHECK_EQ(SP_InsertRec(fd, "x", 0, &r), RECE_RECSIZE);
    memset(buf, 'z', sizeof buf);
    CHECK_EQ(SP_InsertRec(fd, buf, SP_MAX_RECORD + 1, &r), RECE_RECSIZE);
    CHECK_EQ(SP_GetRec(fd, rid(5, 0), buf, sizeof buf, &len), RECE_INVALIDRID);
    CHECK_EQ(SP_GetRec(fd, rid(0, 7), buf, sizeof buf, &len), RECE_INVALIDRID);
    CHECK_EQ(SP_GetRec(fd, -3, buf, sizeof buf, &len), RECE_INVALIDRID);
    CHECK_EQ(SP_DeleteRec(fd, rid(0, 1)), RECE_INVALIDRID);
    CHECK_EQ(SP_InsertRec(99, "x", 1, &r), RECE_FD);
    CHECK_EQ(SP_DeleteRec(-1, 0), RECE_FD);

    /* A maximum-size record fills a page exactly. */
    CHECK_EQ(SP_InsertRec(fd, buf, SP_MAX_RECORD, &r), RECE_OK);
    CHECK_EQ(r, rid(1, 0));
    CHECK_EQ(page_free(fd, 1), 0);
    CHECK_EQ(SP_InsertRec(fd, "y", 1, &r), RECE_OK);
    CHECK_EQ(r, rid(0, 1));                 /* first fit goes back to page 0 */
    CHECK_EQ(SP_CloseFile(fd), PFE_OK);
    CHECK_EQ(SP_CloseFile(fd), RECE_FD);
    CHECK_EQ(SP_DestroyFile("basic"), PFE_OK);

    /* A PF file whose pages are not slotted pages is refused. */
    {
        int pf, page;
        char *data;
        CHECK_EQ(PF_CreateFile("junk"), PFE_OK);
        CHECK((pf = PF_OpenFile("junk")) >= 0);
        CHECK_EQ(PF_AllocPage(pf, &page, &data), PFE_OK);
        memset(data, 0xAB, PF_PAGE_SIZE);
        CHECK_EQ(PF_UnfixPage(pf, page, TRUE), PFE_OK);
        CHECK_EQ(PF_CloseFile(pf), PFE_OK);
        CHECK_EQ(SP_OpenFile("junk", PF_POLICY_LRU, search_mode), RECE_BADPAGE);
        CHECK_EQ(PF_DestroyFile("junk"), PFE_OK);
    }
    printf("  insert/get/error handling ok\n");
}

/* Walks through first fit, compaction, slot reuse, trimming, I/O counts and
 * persistence on one file with exactly predictable placements. */
static void test_layout_sequence(void)
{
    Shadow s = {0};
    PF_Stats st;
    SP_PageInfo info;
    char before[PF_PAGE_SIZE];
    int fd, i, len;

    CHECK_EQ(SP_CreateFile("layout"), PFE_OK);
    CHECK((fd = SP_OpenFile("layout", PF_POLICY_LRU, search_mode)) >= 0);

    /* 80 records of 100 bytes: 39 on page 0, 39 on page 1, 2 on page 2. */
    for (i = 0; i < 80; i++)
        insert_ok(fd, &s, 100, i, rid(i / 39, i % 39));
    CHECK_EQ(SP_NumPages(fd), 3);
    CHECK_EQ(page_free(fd, 0), 32);
    verify(fd, &s);
    printf("  fill pages ok\n");

    /* First fit: a 28-byte record (+4 slot) exactly fills page 0's 32 bytes;
     * the next record no longer fits there and goes to page 1, not page 2. */
    insert_ok(fd, &s, 28, 1000, rid(0, 39));
    CHECK_EQ(page_free(fd, 0), 0);
    insert_ok(fd, &s, 1, 1001, rid(1, 39));
    CHECK_EQ(page_free(fd, 1), 27);
    printf("  first fit ok\n");

    /* Compaction on delete: removing slot 10 frees its 100 bytes as one
     * contiguous region; slot 10 stays in the directory (it is not last),
     * and every other record on the page keeps its RID and bytes. */
    delete_ok(fd, &s, rid(0, 10));
    CHECK_EQ(SP_GetPageInfo(fd, 0, &info), RECE_OK);
    CHECK_EQ(info.numSlots, 40);
    CHECK_EQ(info.liveCount, 39);
    CHECK_EQ(info.freeEnd - info.freeStart, 100);
    CHECK_EQ(SP_GetRec(fd, rid(0, 10), before, sizeof before, &len), RECE_INVALIDRID);
    CHECK_EQ(SP_DeleteRec(fd, rid(0, 10)), RECE_INVALIDRID);
    verify(fd, &s);
    printf("  delete + compaction ok\n");

    /* Slot reuse: the next record that fits page 0 takes empty slot 10 and
     * needs no new directory entry (60 bytes from 100 free leaves 40). */
    insert_ok(fd, &s, 60, 1002, rid(0, 10));
    CHECK_EQ(page_free(fd, 0), 40);
    printf("  slot reuse ok\n");

    /* Deleting the last slot trims the directory: 40 + 28 + 4 = 72 free. */
    delete_ok(fd, &s, rid(0, 39));
    CHECK_EQ(SP_GetPageInfo(fd, 0, &info), RECE_OK);
    CHECK_EQ(info.numSlots, 39);
    CHECK_EQ(info.freeEnd - info.freeStart, 72);
    insert_ok(fd, &s, 68, 1003, rid(0, 39));      /* 68 + new slot 4 = 72 */
    CHECK_EQ(page_free(fd, 0), 0);
    verify(fd, &s);
    printf("  directory trimming ok\n");

    /* I/O accounting: one insert = one update notification.  The cached
     * search fetches only the chosen page; the scan search also fetches the
     * full page 0 first.  Delete = 1 read + 1 write, lookup = 1 read. */
    PF_ResetStats();
    insert_ok(fd, &s, 10, 1004, rid(1, 40));
    st = PF_GetStats();
    CHECK_EQ(st.logical_reads, search_mode == REC_SEARCH_CACHED ? 1 : 2);
    CHECK_EQ(st.logical_writes, 1);
    PF_ResetStats();
    delete_ok(fd, &s, rid(1, 40));
    st = PF_GetStats();
    CHECK_EQ(st.logical_reads, 1);
    CHECK_EQ(st.logical_writes, 1);
    PF_ResetStats();
    CHECK_EQ(SP_GetRec(fd, rid(1, 0), before, sizeof before, &len), RECE_OK);
    st = PF_GetStats();
    CHECK_EQ(st.logical_reads, 1);
    CHECK_EQ(st.logical_writes, 0);
    printf("  dirty marking / I/O accounting ok\n");

    /* Persistence: dirty pages reach the file and the free-space cache is
     * rebuilt on reopen (the next insert lands where first fit says). */
    CHECK_EQ(SP_CloseFile(fd), PFE_OK);
    CHECK((fd = SP_OpenFile("layout", PF_POLICY_MRU, search_mode)) >= 0);
    CHECK_EQ(SP_NumPages(fd), 3);
    verify(fd, &s);
    insert_ok(fd, &s, 20, 1005, rid(1, 40));      /* page 0 full, page 1 has 27 */
    verify(fd, &s);
    CHECK_EQ(SP_CloseFile(fd), PFE_OK);
    CHECK_EQ(SP_DestroyFile("layout"), PFE_OK);
    shadow_free(&s);
    printf("  persistence ok\n");
}

/* Random inserts/deletes against two files, one per search mode: both must
 * produce identical RIDs at every step and match the shadow throughout. */
static void test_random_against_shadow(void)
{
    Shadow s = {0};
    int fdc, fds, op, r1, r2;
    char buf[PF_PAGE_SIZE];

    CHECK_EQ(SP_CreateFile("rand_c"), PFE_OK);
    CHECK_EQ(SP_CreateFile("rand_s"), PFE_OK);
    CHECK((fdc = SP_OpenFile("rand_c", PF_POLICY_LRU, REC_SEARCH_CACHED)) >= 0);
    CHECK((fds = SP_OpenFile("rand_s", PF_POLICY_MRU, REC_SEARCH_SCAN)) >= 0);
    test_rng = 777;
    for (op = 1; op <= 4000; op++) {
        if (s.n == 0 || (s.n < 1500 && test_rand() % 100 < 65)) {
            int len = test_rand() % 100 == 0 ? 1000 + (int)(test_rand() % (SP_MAX_RECORD - 999))
                                             : 1 + (int)(test_rand() % 300);
            make_record(buf, len, op);
            CHECK_EQ(SP_InsertRec(fdc, buf, len, &r1), RECE_OK);
            CHECK_EQ(SP_InsertRec(fds, buf, len, &r2), RECE_OK);
            CHECK_EQ(r1, r2);
            shadow_add(&s, r1, buf, len);
        } else {
            int i = (int)(test_rand() % s.n);
            CHECK_EQ(SP_DeleteRec(fdc, s.rid[i]), RECE_OK);
            CHECK_EQ(SP_DeleteRec(fds, s.rid[i]), RECE_OK);
            shadow_remove(&s, i);
        }
        if (op % 500 == 0) {
            verify(fdc, &s);
            verify(fds, &s);
        }
    }
    CHECK_EQ(SP_CloseFile(fdc), PFE_OK);
    CHECK_EQ(SP_CloseFile(fds), PFE_OK);
    CHECK((fdc = SP_OpenFile("rand_c", PF_POLICY_LRU, search_mode)) >= 0);
    verify(fdc, &s);
    CHECK_EQ(SP_CloseFile(fdc), PFE_OK);
    CHECK_EQ(SP_DestroyFile("rand_c"), PFE_OK);
    CHECK_EQ(SP_DestroyFile("rand_s"), PFE_OK);
    shadow_free(&s);
    printf("  4000 random operations ok (cached and scan search agree)\n");
}

int main(int argc, char **argv)
{
    int pages = argc > 1 ? atoi(argv[1]) : 20;
    search_mode = argc > 2 && !strcmp(argv[2], "scan") ? REC_SEARCH_SCAN : REC_SEARCH_CACHED;
    CHECK_EQ(PF_InitWithBufferSize(pages), PFE_OK);
    printf("testslotted: %d buffer pages, %s search\n", pages,
           search_mode == REC_SEARCH_SCAN ? "scan" : "cached");
    test_rid_encoding();
    test_basic_and_errors();
    test_layout_sequence();
    test_random_against_shadow();
    printf("testslotted: all checks passed\n");
    return 0;
}
