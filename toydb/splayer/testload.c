/* testload.c: loads the real student file into both layouts and verifies
 * every record byte-for-byte.
 *
 * usage: testload <student.txt> <buffer_pages> <expected_records> <L> [<L> ...]
 *
 * For the slotted file and for a fixed-length file per L it checks that
 *   - every accepted record reads back identically through its RID,
 *   - a sequential scan returns the same multiset (count, bytes, hash sum),
 *   - fixed-length rejects exactly the records longer than L,
 *   - the space statistics account for every byte of every page. */
#include "slotted.h"
#include "fixedrec.h"
#include "studentdata.h"
#include "testutil.h"

typedef struct Totals { long count, bytes; unsigned long long hash; } Totals;

static void add(Totals *t, const char *rec, int len)
{
    t->count++;
    t->bytes += len;
    t->hash += SD_Hash(rec, len);
}

static void check_stats(const REC_SpaceStats *st, const Totals *t)
{
    CHECK_EQ(st->recordBytes + st->metadataBytes + st->paddingBytes + st->freeBytes,
             st->numPages * (long)PF_PAGE_SIZE);
    CHECK_EQ(st->numRecords, t->count);
    CHECK_EQ(st->recordBytes, t->bytes);
}

static void load_slotted(const StudentData *d, int *rids)
{
    Totals in = {0}, out = {0};
    REC_SpaceStats st;
    SP_Scan sc;
    char buf[PF_PAGE_SIZE];
    int fd, i, len, r, err;

    CHECK_EQ(SP_CreateFile("students.sp"), PFE_OK);
    CHECK((fd = SP_OpenFile("students.sp", PF_POLICY_LRU, REC_SEARCH_CACHED)) >= 0);
    for (i = 0; i < d->count; i++) {
        CHECK_EQ(SP_InsertRec(fd, d->rec[i], d->len[i], &rids[i]), RECE_OK);
        add(&in, d->rec[i], d->len[i]);
    }
    CHECK_EQ(SP_CloseFile(fd), PFE_OK);           /* force everything to disk */
    CHECK((fd = SP_OpenFile("students.sp", PF_POLICY_MRU, REC_SEARCH_CACHED)) >= 0);
    for (i = 0; i < d->count; i++) {
        CHECK_EQ(SP_GetRec(fd, rids[i], buf, sizeof buf, &len), RECE_OK);
        CHECK(len == d->len[i] && memcmp(buf, d->rec[i], len) == 0);
    }
    CHECK_EQ(SP_ScanOpen(fd, &sc), RECE_OK);
    while ((err = SP_ScanNext(&sc, &r, buf, sizeof buf, &len)) == RECE_OK)
        add(&out, buf, len);
    CHECK_EQ(err, RECE_EOF);
    SP_ScanClose(&sc);
    CHECK(out.count == in.count && out.bytes == in.bytes && out.hash == in.hash);
    CHECK_EQ(SP_GetSpaceStats(fd, &st), RECE_OK);
    check_stats(&st, &in);
    printf("  slotted: %ld records, %ld pages, utilization %.2f%%\n",
           st.numRecords, st.numPages, 100 * REC_Utilization(&st));
    CHECK_EQ(SP_CloseFile(fd), PFE_OK);
    CHECK_EQ(SP_DestroyFile("students.sp"), PFE_OK);
}

static void load_fixed(const StudentData *d, int *rids, int recLen)
{
    Totals in = {0}, out = {0};
    REC_SpaceStats st;
    FX_Scan sc;
    char buf[PF_PAGE_SIZE];
    int fd, i, len, r, err, rejected = 0, expectRejected = 0;

    CHECK_EQ(FX_CreateFile("students.fx"), PFE_OK);
    CHECK((fd = FX_OpenFile("students.fx", PF_POLICY_LRU, recLen, REC_SEARCH_CACHED)) >= 0);
    for (i = 0; i < d->count; i++) {
        err = FX_InsertRec(fd, d->rec[i], d->len[i], &rids[i]);
        expectRejected += d->len[i] > recLen;
        if (err == RECE_RECSIZE) {
            CHECK(d->len[i] > recLen);
            rids[i] = -1;
            rejected++;
        } else {
            CHECK_EQ(err, RECE_OK);
            add(&in, d->rec[i], d->len[i]);
        }
    }
    CHECK_EQ(rejected, expectRejected);
    CHECK_EQ(FX_CloseFile(fd), PFE_OK);
    CHECK((fd = FX_OpenFile("students.fx", PF_POLICY_MRU, recLen, REC_SEARCH_CACHED)) >= 0);
    for (i = 0; i < d->count; i++) {
        if (rids[i] < 0)
            continue;
        CHECK_EQ(FX_GetRec(fd, rids[i], buf, sizeof buf, &len), RECE_OK);
        CHECK(len == d->len[i] && memcmp(buf, d->rec[i], len) == 0);
    }
    CHECK_EQ(FX_ScanOpen(fd, &sc), RECE_OK);
    while ((err = FX_ScanNext(&sc, &r, buf, sizeof buf, &len)) == RECE_OK)
        add(&out, buf, len);
    CHECK_EQ(err, RECE_EOF);
    FX_ScanClose(&sc);
    CHECK(out.count == in.count && out.bytes == in.bytes && out.hash == in.hash);
    CHECK_EQ(FX_GetSpaceStats(fd, &st), RECE_OK);
    check_stats(&st, &in);
    printf("  fixed L=%d: %ld records, %d rejected, %ld pages, utilization %.2f%%\n",
           recLen, st.numRecords, rejected, st.numPages, 100 * REC_Utilization(&st));
    CHECK_EQ(FX_CloseFile(fd), PFE_OK);
    CHECK_EQ(FX_DestroyFile("students.fx"), PFE_OK);
}

int main(int argc, char **argv)
{
    StudentData d;
    int *rids, i;

    if (argc < 5) {
        fprintf(stderr, "usage: %s <student.txt> <buffer_pages> <expected_records> <L>...\n", argv[0]);
        return 2;
    }
    CHECK_EQ(PF_InitWithBufferSize(atoi(argv[2])), PFE_OK);
    CHECK_EQ(SD_Load(argv[1], 1, &d), 0);
    CHECK_EQ(d.count, atoi(argv[3]));
    printf("testload: %d records, %ld bytes, longest %d bytes\n", d.count, d.totalBytes, d.maxLen);
    CHECK((rids = malloc(d.count * sizeof *rids)) != NULL);
    load_slotted(&d, rids);
    for (i = 4; i < argc; i++)
        load_fixed(&d, rids, atoi(argv[i]));
    free(rids);
    SD_Free(&d);
    printf("testload: all checks passed\n");
    return 0;
}
