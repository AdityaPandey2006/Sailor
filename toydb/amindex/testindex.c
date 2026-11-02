/* testindex.c: correctness tests for index Methods 1 and 2.
 *
 *   testindex DATA WORKDIR BUFSIZE [LARGE_N]
 *
 * DATA is student.txt.  Every test builds its files in WORKDIR and removes
 * them afterwards.  LARGE_N (default 150000, 0 to skip) is the size of a
 * synthetic Student dataset big enough to split internal nodes, so the tree
 * gets three levels (the real file only needs two).
 *
 * Checks, for each method:
 *   - the tree satisfies every B+ tree invariant (IX_InspectTree)
 *   - entries == records indexed == dataset records with a roll-no
 *   - existing / non-existing / several roll-nos are found or not found,
 *     and every RID points to a Student record with that roll-no
 *     (IX_Validate checks this for every distinct roll-no)
 *   - the index survives close + reopen (validation and queries reopen it)
 *   - Methods 1 and 2 return identical answers to the same queries
 *   - Method 2's index is valid at checkpoints during construction
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amwrap.h"
#include "slotted.h"
#include "ixindex.h"
#include "testutil.h"

static const char *workdir;

static void path_for(char *out, int size, const char *tag)
{
    snprintf(out, size, "%s/%s_students", workdir, tag);
}

/* ------------------------------------------------------- unit: keys */
static void test_keys(void)
{
    char key[IX_KEY_LEN], s[IX_KEY_LEN + 1];
    const char *r1 = "949981;95302001;XX;M;;";
    const char *r2 = "1;;X";
    const char *r3 = "1;123456789;X";       /* 9 chars: too long */
    const char *r4 = "no-separator";
    const char *r5 = "7;96D07001";          /* last field, no trailing ';' */

    CHECK_EQ(IX_ExtractKey(r1, strlen(r1), key), IXE_OK);
    CHECK(strcmp(IX_KeyString(key, s), "95302001") == 0);
    CHECK_EQ(IX_ExtractKey(r2, strlen(r2), key), IXE_NOKEY);
    CHECK_EQ(IX_ExtractKey(r3, strlen(r3), key), IXE_NOKEY);
    CHECK_EQ(IX_ExtractKey(r4, strlen(r4), key), IXE_NOKEY);
    CHECK_EQ(IX_ExtractKey(r5, strlen(r5), key), IXE_OK);
    CHECK(strcmp(IX_KeyString(key, s), "96D07001") == 0);
    CHECK_EQ(IX_ExtractKey("1;AB", 4, key), IXE_OK);   /* short key: '\0' pad */
    CHECK(key[2] == '\0' && key[7] == '\0');
    printf("  keys: ok\n");
}

/* ------------------------------------------- helpers on an open index */

/* Number of RIDs an EQUAL scan returns; each must point to a record with
 * that roll-no (checked when spfd >= 0). */
static long search(int spfd, int amfd, const char *roll)
{
    char key[IX_KEY_LEN], rec[SP_MAX_RECORD + 1], got[IX_KEY_LEN];
    int sd, rid, len;
    long n = 0;

    memset(key, 0, sizeof key);
    memcpy(key, roll, strlen(roll));
    CHECK((sd = IX_OpenScan(amfd, EQUAL, key)) >= 0);
    while ((rid = AM_FindNextEntry(sd)) >= 0) {
        n++;
        if (spfd >= 0) {
            CHECK_EQ(SP_GetRec(spfd, rid, rec, sizeof rec, &len), RECE_OK);
            CHECK_EQ(IX_ExtractKey(rec, len, got), IXE_OK);
            CHECK(memcmp(got, key, IX_KEY_LEN) == 0);
        }
    }
    CHECK_EQ(rid, AME_EOF);
    CHECK_EQ(AM_CloseIndexScan(sd), AME_OK);
    return n;
}

static long count_key(const StudentData *d, const char *roll)
{
    char key[IX_KEY_LEN], want[IX_KEY_LEN];
    long i, n = 0;
    memset(want, 0, sizeof want);
    memcpy(want, roll, strlen(roll));
    for (i = 0; i < d->count; i++)
        if (IX_ExtractKey(d->rec[i], d->len[i], key) == IXE_OK &&
            memcmp(key, want, IX_KEY_LEN) == 0)
            n++;
    return n;
}

static long usable_keys(const StudentData *d)
{
    char key[IX_KEY_LEN];
    long i, n = 0;
    for (i = 0; i < d->count; i++)
        n += IX_ExtractKey(d->rec[i], d->len[i], key) == IXE_OK;
    return n;
}

/* Explicit searches on a freshly reopened index (deliverable 6.1-6.4, 6.7). */
static void spot_checks(const char *studentFile, const StudentData *d,
                        const char **rolls, int nRolls)
{
    char name[1100];
    int spfd, amfd, i;

    IX_IndexFileName(studentFile, IX_INDEX_NO, name, sizeof name);
    CHECK((spfd = SP_OpenFile((char *)studentFile, PF_POLICY_LRU, REC_SEARCH_SCAN)) >= 0);
    CHECK((amfd = PF_OpenFile(name)) >= 0);
    for (i = 0; i < nRolls; i++)
        CHECK_EQ(search(spfd, amfd, rolls[i]), count_key(d, rolls[i]));
    CHECK_EQ(search(spfd, amfd, "00000000"), 0);       /* non-existing */
    CHECK_EQ(search(spfd, amfd, "~~~~~~~~"), 0);
    CHECK_EQ(search(spfd, amfd, "9530200Z"), 0);
    CHECK_EQ(PF_CloseFile(amfd), PFE_OK);
    CHECK_EQ(SP_CloseFile(spfd), RECE_OK);
}

/* ---------------------------------------- Method 2 progress checkpoints */
typedef struct Progress {
    const StudentData *d;
    long checkpoints;
} Progress;

static int on_progress(void *ctx, int spfd, int amfd, long inserted)
{
    Progress *p = ctx;
    IX_TreeInfo t;
    char key[IX_KEY_LEN], s[IX_KEY_LEN + 1];
    long i, want = 0;

    for (i = 0; i < inserted; i++)
        want += IX_ExtractKey(p->d->rec[i], p->d->len[i], key) == IXE_OK;
    CHECK_EQ(IX_InspectTreeFd(amfd, &t), IXE_OK);
    CHECK_EQ(t.entries, want);
    /* the newest record and a few older ones must already be searchable */
    for (i = inserted - 1; i >= 0; i -= 1 + inserted / 7)
        if (IX_ExtractKey(p->d->rec[i], p->d->len[i], key) == IXE_OK)
            CHECK(search(spfd, amfd, IX_KeyString(key, s)) >= 1);
    p->checkpoints++;
    return 0;
}

/* -------------------------------------------- one dataset, both methods */
static void run_dataset(const char *label, const StudentData *d, int minHeight,
                        const char **rolls, int nRolls)
{
    char f1[1024], f2[1024];
    IX_BuildSpec spec;
    IX_BuildResult r1, r2;
    IX_TreeInfo t1, t2;
    IX_ValidateResult v;
    IX_QuerySet q;
    IX_QueryResult q1, q2, q1b;
    Progress prog = { d, 0 };
    long expect = usable_keys(d);

    printf("  dataset %s: %d records, %ld with a roll-no\n", label, d->count, expect);
    path_for(f1, sizeof f1, "t_m1");
    path_for(f2, sizeof f2, "t_m2");
    IX_RemoveFiles(f1, IX_INDEX_NO);
    IX_RemoveFiles(f2, IX_INDEX_NO);
    spec.data = d;
    spec.indexNo = IX_INDEX_NO;
    spec.policy = PF_POLICY_LRU;

    /* Method 1 */
    spec.studentFile = f1;
    CHECK_EQ(IX_Method1(&spec, &r1), IXE_OK);
    CHECK_EQ(r1.recordsTotal, d->count);
    CHECK_EQ(r1.recordsIndexed, expect);
    CHECK_EQ(r1.recordsSkipped, d->count - expect);
    CHECK_EQ(IX_InspectTree(f1, IX_INDEX_NO, &t1), IXE_OK);
    CHECK_EQ(t1.entries, expect);
    CHECK(t1.height >= minHeight);
    CHECK_EQ(t1.rootMaxKeys, 340);   /* AM compiled with 4096-byte pages */
    CHECK_EQ(IX_Validate(f1, IX_INDEX_NO, d, &v), IXE_OK);
    CHECK_EQ(v.scannedEntries, expect);
    printf("    method 1: height %d, %ld internal + %ld leaf pages, %ld keys, "
           "%ld entries, %ld duplicate keys: valid\n", t1.height, t1.internalPages,
           t1.leafPages, t1.keys, t1.entries, v.duplicateKeys);

    /* Method 2, checking the index at 20 points during construction */
    spec.studentFile = f2;
    CHECK_EQ(IX_BuildIncremental(&spec, &r2, on_progress, &prog,
                                 d->count / 20 ? d->count / 20 : 1), IXE_OK);
    CHECK(prog.checkpoints >= 1);
    CHECK_EQ(r2.recordsTotal, d->count);
    CHECK_EQ(r2.recordsIndexed, expect);
    CHECK_EQ(IX_InspectTree(f2, IX_INDEX_NO, &t2), IXE_OK);
    CHECK_EQ(t2.entries, expect);
    CHECK(t2.height >= minHeight);
    CHECK_EQ(IX_Validate(f2, IX_INDEX_NO, d, &v), IXE_OK);
    printf("    method 2: height %d, %ld internal + %ld leaf pages, %ld keys, "
           "%ld entries, valid at %ld checkpoints and at the end\n", t2.height,
           t2.internalPages, t2.leafPages, t2.keys, t2.entries, prog.checkpoints);

    /* explicit searches after reopening */
    spot_checks(f1, d, rolls, nRolls);
    spot_checks(f2, d, rolls, nRolls);

    /* both methods insert the same records in the same order into the same
     * kind of Student file, so the RIDs - and all answers - must agree */
    CHECK_EQ(IX_MakeQueries(d, 500, 100, 7, &q), IXE_OK);
    CHECK_EQ(IX_RunQueries(f1, IX_INDEX_NO, &q, &q1), IXE_OK);
    CHECK_EQ(IX_RunQueries(f2, IX_INDEX_NO, &q, &q2), IXE_OK);
    CHECK_EQ(IX_RunQueries(f1, IX_INDEX_NO, &q, &q1b), IXE_OK);   /* reopen again */
    CHECK_EQ(q1.queries, 600);
    CHECK_EQ(q1.notFound, 100);
    CHECK(q1.ridSum == q2.ridSum && q1.rids == q2.rids);
    CHECK(q1.ridSum == q1b.ridSum);
    CHECK(q1.m.pf.logical_reads == q1b.m.pf.logical_reads);
    CHECK(q1.m.pf.logical_reads > 0);
    printf("    queries: %ld found, %ld not found, identical answers from both "
           "methods; %llu pages accessed (method 1)\n", q1.found, q1.notFound,
           q1.m.pf.logical_reads);
    IX_FreeQueries(&q);

    IX_RemoveFiles(f1, IX_INDEX_NO);
    IX_RemoveFiles(f2, IX_INDEX_NO);
}

/* -------------------------------------------- synthetic large dataset */
static void make_large(StudentData *d, int n)
{
    int i, *perm;
    char line[128];

    memset(d, 0, sizeof *d);
    CHECK((d->rec = malloc(n * sizeof *d->rec)) && (d->len = malloc(n * sizeof *d->len)));
    CHECK((perm = malloc(n * sizeof *perm)) != NULL);
    for (i = 0; i < n; i++)
        perm[i] = i;
    for (i = n - 1; i > 0; i--) {              /* random insertion order */
        int j = test_rand() % (i + 1), t = perm[i];
        perm[i] = perm[j];
        perm[j] = t;
    }
    for (i = 0; i < n; i++) {
        int k = perm[i], len;
        if (k % 997 == 0)                       /* some records without a roll-no */
            len = snprintf(line, sizeof line, "%d;;XXXXXXXXX;M;;;", k);
        else if (k % 1009 < 40)                 /* blocks of 40 duplicate roll-nos */
            len = snprintf(line, sizeof line, "%d;D%07d;XXXXXXXXX;F;;;", k, k / 1009);
        else
            len = snprintf(line, sizeof line, "%d;S%07d;XXXXXXXXX;M;XXXX;;1;BTECH;;;", k, k);
        CHECK((d->rec[i] = malloc(len)) != NULL);
        memcpy(d->rec[i], line, len);
        d->len[i] = len;
        d->totalBytes += len;
        if (len > d->maxLen)
            d->maxLen = len;
        d->count++;
    }
    free(perm);
}

int main(int argc, char **argv)
{
    StudentData d;
    int bufsize, largeN;
    const char *rolls[] = { "95302001", "96D07001", "97305818", "99302001",
                            "99310001", "S0000001" };
    const char *largeRolls[] = { "S0000001", "S0012345", "D0000003", "D0000100" };

    if (argc < 4) {
        fprintf(stderr, "usage: testindex DATA WORKDIR BUFSIZE [LARGE_N]\n");
        return 2;
    }
    workdir = argv[2];
    bufsize = atoi(argv[3]);
    largeN = argc > 4 ? atoi(argv[4]) : 150000;
    CHECK_EQ(PF_InitWithBufferSize(bufsize), PFE_OK);
    printf("testindex: buffer pool %d frames\n", bufsize);

    test_keys();

    CHECK(SD_Load(argv[1], 1, &d) == 0);
    CHECK_EQ(count_key(&d, "97305818"), 2);     /* duplicates in the real data */
    run_dataset("student.txt", &d, 2, rolls, 6);
    SD_Free(&d);

    if (largeN > 0) {
        make_large(&d, largeN);
        run_dataset("synthetic", &d, 3, largeRolls, 4);
        SD_Free(&d);
    }
    printf("testindex: all tests passed\n");
    return 0;
}
