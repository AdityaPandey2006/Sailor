/* spbench.c: one storage experiment on the student file; prints one JSON line.
 *
 * usage: spbench <slotted|fixed> <L> <student.txt> <skip_lines> <buffer_pages>
 *                <lru|mru> <cached|scan> <delete_pct> <seed>
 *
 * L is the fixed record length (ignored for slotted).  Measured phases, each
 * with freshly reset PF counters and a monotonic clock:
 *
 *   load      insert every record into a new, empty file (records longer
 *             than L are rejected by the fixed layout), then close it
 *   scan      sequential scan of all records (read-only)
 *   delete    delete delete_pct% of the stored records, then close
 *   reinsert  insert the deleted records again, then close
 *
 * Every modifying phase ends by closing the file, so all of its dirty pages
 * are written inside the measured region.  Reopening the file (which scans it
 * once to rebuild the free-space cache), reading space statistics and
 * verifying contents happen between phases and are not measured.
 *
 * The deleted records are chosen by a seeded shuffle of the stored records,
 * so every layout and search mode deletes the same students.  After the scan
 * and after reinsert the file's contents are compared with what was loaded
 * (count, bytes and an order-independent hash); "verified" reports this.
 * Run in a scratch directory: the file bench.db is created and destroyed. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "slotted.h"
#include "fixedrec.h"
#include "studentdata.h"
#include "testutil.h"

#define DB_NAME "bench.db"

typedef struct Totals { long count, bytes; unsigned long long hash; } Totals;

/* The two layers expose the same operations; this table lets one driver
 * measure both without mixing their implementations. */
typedef struct Layer {
    int (*open)(int policy, int recLen, int search);
    int (*insert)(int, const char *, int, int *);
    int (*del)(int, int);
    int (*stats)(int, REC_SpaceStats *);
    int (*close)(int);
    void (*scan)(int, Totals *);
} Layer;

static void add(Totals *t, const char *rec, int len)
{
    t->count++;
    t->bytes += len;
    t->hash += SD_Hash(rec, len);
}

static int same(const Totals *a, const Totals *b)
{
    return a->count == b->count && a->bytes == b->bytes && a->hash == b->hash;
}

static int sp_open(int policy, int recLen, int search)
{
    (void)recLen;
    return SP_OpenFile(DB_NAME, policy, search);
}

static int fx_open(int policy, int recLen, int search)
{
    return FX_OpenFile(DB_NAME, policy, recLen, search);
}

static void sp_scan_all(int fd, Totals *t)
{
    SP_Scan sc; char buf[PF_PAGE_SIZE]; int r, len, err;
    CHECK_EQ(SP_ScanOpen(fd, &sc), RECE_OK);
    while ((err = SP_ScanNext(&sc, &r, buf, sizeof buf, &len)) == RECE_OK)
        add(t, buf, len);
    CHECK_EQ(err, RECE_EOF);
    SP_ScanClose(&sc);
}

static void fx_scan_all(int fd, Totals *t)
{
    FX_Scan sc; char buf[PF_PAGE_SIZE]; int r, len, err;
    CHECK_EQ(FX_ScanOpen(fd, &sc), RECE_OK);
    while ((err = FX_ScanNext(&sc, &r, buf, sizeof buf, &len)) == RECE_OK)
        add(t, buf, len);
    CHECK_EQ(err, RECE_EOF);
    FX_ScanClose(&sc);
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static double phase_start;
static void begin(void) { PF_ResetStats(); phase_start = now_ms(); }

/* Prints the counters and elapsed time of the phase just finished. */
static void end(const char *name, int first)
{
    double ms = now_ms() - phase_start;
    PF_Stats s = PF_GetStats();
    printf("%s\"%s\":{\"ms\":%.3f,\"logical_reads\":%llu,\"logical_writes\":%llu,"
           "\"physical_reads\":%llu,\"physical_writes\":%llu,\"buffer_hits\":%llu,"
           "\"buffer_misses\":%llu}",
           first ? "" : ",", name, ms, s.logical_reads, s.logical_writes,
           s.physical_reads, s.physical_writes, s.buffer_hits, s.buffer_misses);
}

static void print_space(const char *name, const REC_SpaceStats *s, int first)
{
    printf("%s\"%s\":{\"pages\":%ld,\"records\":%ld,\"record_bytes\":%ld,"
           "\"metadata_bytes\":%ld,\"padding_bytes\":%ld,\"free_bytes\":%ld,"
           "\"utilization\":%.6f}",
           first ? "" : ",", name, s->numPages, s->numRecords, s->recordBytes,
           s->metadataBytes, s->paddingBytes, s->freeBytes, REC_Utilization(s));
}

int main(int argc, char **argv)
{
    static const Layer slotted = {sp_open, SP_InsertRec, SP_DeleteRec, SP_GetSpaceStats,
                                  SP_CloseFile, sp_scan_all};
    static const Layer fixed = {fx_open, FX_InsertRec, FX_DeleteRec, FX_GetSpaceStats,
                                FX_CloseFile, fx_scan_all};
    const Layer *ly;
    StudentData d;
    REC_SpaceStats space[3];          /* after load, delete, reinsert */
    Totals loaded = {0}, check;
    int isFixed, recLen, bufPages, policy, search, deletePct, fd, i, err;
    int stored = 0, rejected = 0, nDelete, verified = 1;
    int *rids, *order;
    unsigned long seed;

    if (argc != 10) {
        fprintf(stderr, "usage: %s <slotted|fixed> <L> <student.txt> <skip_lines> <buffer_pages>"
                        " <lru|mru> <cached|scan> <delete_pct> <seed>\n", argv[0]);
        return 2;
    }
    isFixed = !strcmp(argv[1], "fixed");
    recLen = atoi(argv[2]);
    bufPages = atoi(argv[5]);
    policy = !strcmp(argv[6], "mru") ? PF_POLICY_MRU : PF_POLICY_LRU;
    search = !strcmp(argv[7], "scan") ? REC_SEARCH_SCAN : REC_SEARCH_CACHED;
    deletePct = atoi(argv[8]);
    seed = strtoul(argv[9], NULL, 10);
    if ((!isFixed && strcmp(argv[1], "slotted")) || (isFixed && FX_SlotsPerPage(recLen) == 0) ||
        bufPages < 1 || deletePct < 0 || deletePct > 100) {
        fprintf(stderr, "spbench: invalid arguments\n");
        return 2;
    }
    ly = isFixed ? &fixed : &slotted;

    CHECK_EQ(PF_InitWithBufferSize(bufPages), PFE_OK);
    CHECK_EQ(SD_Load(argv[3], atoi(argv[4]), &d), 0);
    CHECK((rids = malloc(d.count * sizeof *rids)) && (order = malloc(d.count * sizeof *order)));
    CHECK_EQ(PF_CreateFile(DB_NAME), PFE_OK);

    printf("{\"layout\":\"%s\",\"record_length\":%d,\"buffer_pages\":%d,\"policy\":\"%s\","
           "\"search\":\"%s\",\"delete_pct\":%d,\"seed\":%lu,\"phases\":{",
           argv[1], isFixed ? recLen : 0, bufPages, policy == PF_POLICY_MRU ? "MRU" : "LRU",
           search == REC_SEARCH_SCAN ? "scan" : "cached", deletePct, seed);

    /* load (+ close) */
    CHECK((fd = ly->open(policy, recLen, search)) >= 0);
    begin();
    for (i = 0; i < d.count; i++) {
        err = ly->insert(fd, d.rec[i], d.len[i], &rids[i]);
        if (err == RECE_RECSIZE) { rejected++; continue; }   /* fixed: len > L */
        CHECK_EQ(err, RECE_OK);
        order[stored++] = i;
        add(&loaded, d.rec[i], d.len[i]);
    }
    CHECK_EQ(ly->close(fd), PFE_OK);
    end("load", 1);
    CHECK((fd = ly->open(policy, recLen, search)) >= 0);
    CHECK_EQ(ly->stats(fd, &space[0]), RECE_OK);

    /* scan */
    memset(&check, 0, sizeof check);
    begin();
    ly->scan(fd, &check);
    end("scan", 0);
    verified &= same(&check, &loaded);

    /* delete (+ close): seeded Fisher-Yates shuffle picks the victims */
    test_rng = seed;
    for (i = stored - 1; i > 0; i--) {
        int j = (int)(test_rand() % (unsigned)(i + 1)), t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    nDelete = (int)((long)stored * deletePct / 100);
    begin();
    for (i = 0; i < nDelete; i++)
        CHECK_EQ(ly->del(fd, rids[order[i]]), RECE_OK);
    CHECK_EQ(ly->close(fd), PFE_OK);
    end("delete", 0);
    CHECK((fd = ly->open(policy, recLen, search)) >= 0);
    CHECK_EQ(ly->stats(fd, &space[1]), RECE_OK);

    /* reinsert (+ close) */
    begin();
    for (i = 0; i < nDelete; i++)
        CHECK_EQ(ly->insert(fd, d.rec[order[i]], d.len[order[i]], &rids[order[i]]), RECE_OK);
    CHECK_EQ(ly->close(fd), PFE_OK);
    end("reinsert", 0);
    CHECK((fd = ly->open(policy, recLen, search)) >= 0);
    CHECK_EQ(ly->stats(fd, &space[2]), RECE_OK);
    memset(&check, 0, sizeof check);
    ly->scan(fd, &check);
    verified &= same(&check, &loaded);
    CHECK_EQ(ly->close(fd), PFE_OK);

    printf("},\"space\":{");
    print_space("after_load", &space[0], 1);
    print_space("after_delete", &space[1], 0);
    print_space("after_reinsert", &space[2], 0);
    printf("},\"records_input\":%d,\"records_stored\":%d,\"records_rejected\":%d,"
           "\"input_bytes\":%ld,\"max_input_length\":%d,\"deleted\":%d,\"verified\":%s}\n",
           d.count, stored, rejected, d.totalBytes, d.maxLen, nDelete, verified ? "true" : "false");

    PF_DestroyFile(DB_NAME);
    free(rids);
    free(order);
    SD_Free(&d);
    return verified ? 0 : 1;
}
