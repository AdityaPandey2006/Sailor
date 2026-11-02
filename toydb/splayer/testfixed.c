/* testfixed.c: unit tests for the fixed-length (static) record layer.
 *
 * usage: testfixed <buffer_pages> <cached|scan>       (run in a scratch dir)
 *
 * With L = 100 a page holds 40 slots: 8 header + 5 bitmap + 40*100 = 4013
 * bytes, leaving an 83-byte tail that can never hold a record. */
#include "fixedrec.h"
#include "slotted.h"
#include "testutil.h"

static int search_mode;
#define L 100

static int rid(int page, int slot) { return REC_MakeRid(page, slot); }

static void fx_collect(int fd, Shadow *out)
{
    FX_Scan sc;
    char buf[PF_PAGE_SIZE];
    int r, len, err;
    shadow_clear(out);
    CHECK_EQ(FX_ScanOpen(fd, &sc), RECE_OK);
    while ((err = FX_ScanNext(&sc, &r, buf, sizeof buf, &len)) == RECE_OK)
        shadow_add(out, r, buf, len);
    CHECK_EQ(err, RECE_EOF);
    CHECK_EQ(FX_ScanClose(&sc), RECE_OK);
}

static void verify(int fd, const Shadow *s, int recLen)
{
    Shadow scanned = {0};
    REC_SpaceStats st;
    char buf[PF_PAGE_SIZE];
    int i, len, slots = FX_SlotsPerPage(recLen);
    long tail = PF_PAGE_SIZE - (8 + (slots + 7) / 8) - (long)slots * recLen;

    fx_collect(fd, &scanned);
    shadow_check_scan(s, &scanned);
    shadow_free(&scanned);
    for (i = 0; i < s->n; i++) {
        CHECK_EQ(FX_GetRec(fd, s->rid[i], buf, sizeof buf, &len), RECE_OK);
        CHECK_EQ(len, s->len[i]);
        CHECK(memcmp(buf, s->data[i], len) == 0);
    }
    CHECK_EQ(FX_GetSpaceStats(fd, &st), RECE_OK);
    check_space_invariant(&st, s);
    /* padding = unused part of each occupied slot + each page's tail */
    CHECK_EQ(st.paddingBytes, (long)s->n * recLen - shadow_bytes(s) + st.numPages * tail);
    CHECK_EQ(st.freeBytes, ((long)st.numPages * slots - s->n) * recLen);
}

static void insert_ok(int fd, Shadow *s, int len, int tag, int expectRid)
{
    char buf[PF_PAGE_SIZE];
    int r;
    make_record(buf, len, tag);
    CHECK_EQ(FX_InsertRec(fd, buf, len, &r), RECE_OK);
    if (expectRid >= 0)
        CHECK_EQ(r, expectRid);
    shadow_add(s, r, buf, len);
}

static void delete_ok(int fd, Shadow *s, int r)
{
    int i = shadow_find(s, r);
    CHECK(i >= 0);
    CHECK_EQ(FX_DeleteRec(fd, r), RECE_OK);
    shadow_remove(s, i);
}

static void test_slots_per_page(void)
{
    CHECK_EQ(FX_SlotsPerPage(100), 40);
    CHECK_EQ(FX_SlotsPerPage(108), 37);
    CHECK_EQ(FX_SlotsPerPage(1), 3633);     /* 8 + 455 + 3633 = 4096 */
    CHECK_EQ(FX_SlotsPerPage(FX_MAX_RECLEN), 1);
    CHECK_EQ(FX_SlotsPerPage(FX_MAX_RECLEN + 1), 0);
    CHECK_EQ(FX_SlotsPerPage(0), 0);
    printf("  slots per page ok\n");
}

static void test_sequence(void)
{
    Shadow s = {0};
    PF_Stats st;
    char buf[PF_PAGE_SIZE];
    int fd, i, r, len;

    CHECK_EQ(FX_CreateFile("fixed"), PFE_OK);
    CHECK_EQ(FX_OpenFile("fixed", PF_POLICY_LRU, 0, search_mode), RECE_INVALIDARG);
    CHECK((fd = FX_OpenFile("fixed", PF_POLICY_LRU, L, search_mode)) >= 0);

    /* Oversized records are rejected, never truncated; '\0' is reserved. */
    memset(buf, 'q', sizeof buf);
    CHECK_EQ(FX_InsertRec(fd, buf, L + 1, &r), RECE_RECSIZE);
    CHECK_EQ(FX_InsertRec(fd, buf, 0, &r), RECE_RECSIZE);
    buf[5] = '\0';
    CHECK_EQ(FX_InsertRec(fd, buf, 10, &r), RECE_INVALIDARG);
    CHECK_EQ(FX_NumPages(fd), 0);
    CHECK_EQ(FX_InsertRec(42, buf, 1, &r), RECE_FD);

    /* 85 records of varying length (including exactly L): 40 + 40 + 5. */
    for (i = 0; i < 85; i++)
        insert_ok(fd, &s, i == 3 ? L : 1 + (i * 37) % L, i, rid(i / 40, i % 40));
    CHECK_EQ(FX_NumPages(fd), 3);
    CHECK_EQ(FX_GetRec(fd, rid(0, 3), buf, sizeof buf, &len), RECE_OK);
    CHECK_EQ(len, L);
    CHECK_EQ(FX_GetRec(fd, rid(0, 3), buf, 10, &len), RECE_BUFSMALL);
    CHECK_EQ(FX_GetRec(fd, rid(2, 5), buf, sizeof buf, &len), RECE_INVALIDRID);
    CHECK_EQ(FX_GetRec(fd, rid(0, 40), buf, sizeof buf, &len), RECE_INVALIDRID);
    verify(fd, &s, L);
    printf("  insert/get/reject ok\n");

    /* Deleting frees the slot; first fit reuses the lowest free slot on the
     * lowest page with one. */
    delete_ok(fd, &s, rid(1, 0));
    delete_ok(fd, &s, rid(0, 7));
    CHECK_EQ(FX_DeleteRec(fd, rid(0, 7)), RECE_INVALIDRID);
    verify(fd, &s, L);
    insert_ok(fd, &s, 50, 500, rid(0, 7));
    insert_ok(fd, &s, 50, 501, rid(1, 0));
    insert_ok(fd, &s, 50, 502, rid(2, 5));
    verify(fd, &s, L);
    printf("  delete + slot reuse ok\n");

    /* One insert = one update notification; scan search also reads the two
     * full pages before reaching page 2. */
    PF_ResetStats();
    insert_ok(fd, &s, 9, 503, rid(2, 6));
    st = PF_GetStats();
    CHECK_EQ(st.logical_reads, search_mode == REC_SEARCH_CACHED ? 1 : 3);
    CHECK_EQ(st.logical_writes, 1);
    printf("  dirty marking / I/O accounting ok\n");

    /* Persistence, and the record length is checked against the pages. */
    CHECK_EQ(FX_CloseFile(fd), PFE_OK);
    CHECK_EQ(FX_OpenFile("fixed", PF_POLICY_LRU, 108, search_mode), RECE_BADPAGE);
    CHECK_EQ(SP_OpenFile("fixed", PF_POLICY_LRU, search_mode), RECE_BADPAGE);
    CHECK((fd = FX_OpenFile("fixed", PF_POLICY_MRU, L, search_mode)) >= 0);
    verify(fd, &s, L);
    insert_ok(fd, &s, 77, 504, rid(2, 7));
    CHECK_EQ(FX_CloseFile(fd), PFE_OK);
    CHECK_EQ(FX_DestroyFile("fixed"), PFE_OK);

    /* And a slotted file is not accepted as a fixed one. */
    CHECK_EQ(SP_CreateFile("sp"), PFE_OK);
    CHECK((fd = SP_OpenFile("sp", PF_POLICY_LRU, search_mode)) >= 0);
    CHECK_EQ(SP_InsertRec(fd, "abc", 3, &r), RECE_OK);
    CHECK_EQ(SP_CloseFile(fd), PFE_OK);
    CHECK_EQ(FX_OpenFile("sp", PF_POLICY_LRU, L, search_mode), RECE_BADPAGE);
    CHECK_EQ(SP_DestroyFile("sp"), PFE_OK);
    shadow_free(&s);
    printf("  persistence + layout checks ok\n");
}

static void test_random_against_shadow(void)
{
    Shadow s = {0};
    int fdc, fds, op, r1, r2;
    char buf[PF_PAGE_SIZE];

    CHECK_EQ(FX_CreateFile("frand_c"), PFE_OK);
    CHECK_EQ(FX_CreateFile("frand_s"), PFE_OK);
    CHECK((fdc = FX_OpenFile("frand_c", PF_POLICY_LRU, 64, REC_SEARCH_CACHED)) >= 0);
    CHECK((fds = FX_OpenFile("frand_s", PF_POLICY_MRU, 64, REC_SEARCH_SCAN)) >= 0);
    test_rng = 4242;
    for (op = 1; op <= 4000; op++) {
        if (s.n == 0 || (s.n < 1500 && test_rand() % 100 < 65)) {
            int len = 1 + (int)(test_rand() % 80), e1, e2;   /* some exceed 64 */
            make_record(buf, len, op);
            e1 = FX_InsertRec(fdc, buf, len, &r1);
            e2 = FX_InsertRec(fds, buf, len, &r2);
            CHECK_EQ(e1, len > 64 ? RECE_RECSIZE : RECE_OK);
            CHECK_EQ(e2, e1);
            if (e1 == RECE_OK) {
                CHECK_EQ(r1, r2);
                shadow_add(&s, r1, buf, len);
            }
        } else {
            int i = (int)(test_rand() % s.n);
            CHECK_EQ(FX_DeleteRec(fdc, s.rid[i]), RECE_OK);
            CHECK_EQ(FX_DeleteRec(fds, s.rid[i]), RECE_OK);
            shadow_remove(&s, i);
        }
        if (op % 500 == 0) {
            verify(fdc, &s, 64);
            verify(fds, &s, 64);
        }
    }
    CHECK_EQ(FX_CloseFile(fdc), PFE_OK);
    CHECK_EQ(FX_CloseFile(fds), PFE_OK);
    CHECK_EQ(FX_DestroyFile("frand_c"), PFE_OK);
    CHECK_EQ(FX_DestroyFile("frand_s"), PFE_OK);
    shadow_free(&s);
    printf("  4000 random operations ok (cached and scan search agree)\n");
}

int main(int argc, char **argv)
{
    int pages = argc > 1 ? atoi(argv[1]) : 20;
    search_mode = argc > 2 && !strcmp(argv[2], "scan") ? REC_SEARCH_SCAN : REC_SEARCH_CACHED;
    CHECK_EQ(PF_InitWithBufferSize(pages), PFE_OK);
    printf("testfixed: %d buffer pages, %s search\n", pages,
           search_mode == REC_SEARCH_SCAN ? "scan" : "cached");
    test_slots_per_page();
    test_sequence();
    test_random_against_shadow();
    printf("testfixed: all checks passed\n");
    return 0;
}
