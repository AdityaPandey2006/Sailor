/* ixbuild.c: index-construction Methods 1 and 2, and the method table.
 *
 * Both methods use only existing interfaces:
 *   Student file: SP_CreateFile / SP_OpenFile / SP_InsertRec / SP_Scan*
 *   Index:        AM_CreateIndex, PF_OpenFile, AM_InsertEntry, PF_CloseFile
 * The AM layer has no "open index" call: an index is opened by opening its
 * PF file "<studentFile>.<indexNo>" and passing that descriptor to AM.
 */
#include <stdio.h>
#include <string.h>

#include "amwrap.h"
#include "slotted.h"
#include "ixindex.h"

/* Method 3 (bulk loading) is added here by the second developer:
 *   { 3, "bulk", "bulk load from a pre-sorted Student file", IX_Method3 },
 */
const IX_Method IX_Methods[] = {
    { 1, "existing", "index an existing Student file (scan + AM_InsertEntry)",
      IX_Method1 },
    { 2, "incremental", "insert each record, then its index entry",
      IX_Method2 },
    { 0, NULL, NULL, NULL }
};

const IX_Method *IX_FindMethod(int id)
{
    const IX_Method *m;
    for (m = IX_Methods; m->build; m++)
        if (m->id == id)
            return m;
    return NULL;
}

/* Creates the index file and opens it.  Returns a PF descriptor or error. */
static int create_and_open_index(const char *studentFile, int indexNo)
{
    char name[1024];
    int err = AM_CreateIndex((char *)studentFile, indexNo, IX_KEY_TYPE, IX_KEY_LEN);
    if (err != AME_OK)
        return err;
    IX_IndexFileName(studentFile, indexNo, name, sizeof name);
    return PF_OpenFile(name);
}

/* Inserts one record's (roll-no, RID) pair.  A record without a usable
 * roll-no is counted as skipped, not treated as an error. */
static int index_record(int amfd, const char *rec, int len, int rid, IX_BuildResult *r)
{
    char key[IX_KEY_LEN];
    int err;

    if (IX_ExtractKey(rec, len, key) != IXE_OK) {
        r->recordsSkipped++;
        return IXE_OK;
    }
    if ((err = AM_InsertEntry(amfd, IX_KEY_TYPE, IX_KEY_LEN, key, rid)) != AME_OK)
        return err;
    r->recordsIndexed++;
    return IXE_OK;
}

/* Closes what is open, keeping the first error. */
static int close_files(int spfd, int amfd, int err)
{
    if (amfd >= 0) {
        int e = PF_CloseFile(amfd);
        if (err == IXE_OK)
            err = e;
    }
    if (spfd >= 0) {
        int e = SP_CloseFile(spfd);
        if (err == IXE_OK)
            err = e;
    }
    return err;
}

int IX_LoadStudentFile(const StudentData *d, const char *studentFile, int policy,
                       long *stored)
{
    int spfd, err, i, rid;

    *stored = 0;
    if ((err = SP_CreateFile((char *)studentFile)) != RECE_OK)
        return err;
    if ((spfd = SP_OpenFile((char *)studentFile, policy, REC_SEARCH_CACHED)) < 0)
        return spfd;
    for (i = 0, err = RECE_OK; i < d->count && err == RECE_OK; i++)
        if ((err = SP_InsertRec(spfd, d->rec[i], d->len[i], &rid)) == RECE_OK)
            (*stored)++;
    return close_files(spfd, -1, err);
}

/* ------------------------------------------------------------ Method 1 */

int IX_BuildFromExisting(const char *studentFile, int indexNo, int policy,
                         IX_BuildResult *r, IX_Measure *openCost)
{
    IX_Timer t;
    SP_Scan scan;
    char rec[SP_MAX_RECORD + 1];
    int spfd, amfd = -1, rid, len, err, scanOpen = 0;

    r->recordsTotal = r->recordsIndexed = r->recordsSkipped = 0;

    /* 1. open the existing Student file.  SP_OpenFile reads every page once
     *    to validate it; that cost is reported separately in *openCost. */
    IX_TimerStart(&t);
    spfd = SP_OpenFile((char *)studentFile, policy, REC_SEARCH_SCAN);
    IX_TimerStop(&t, openCost);
    if (spfd < 0)
        return spfd;

    IX_TimerStart(&t);
    /* 2. create the B+ tree index on roll-no */
    if ((amfd = create_and_open_index(studentFile, indexNo)) < 0) {
        err = amfd;
        amfd = -1;
        goto done;
    }
    /* 3-5. sequential scan; insert (roll-no, RID) for every record */
    if ((err = SP_ScanOpen(spfd, &scan)) != RECE_OK)
        goto done;
    scanOpen = 1;
    while ((err = SP_ScanNext(&scan, &rid, rec, sizeof rec, &len)) == RECE_OK) {
        r->recordsTotal++;
        if ((err = index_record(amfd, rec, len, rid, r)) != IXE_OK)
            goto done;
    }
    if (err == RECE_EOF)
        err = IXE_OK;

done:
    if (scanOpen)
        SP_ScanClose(&scan);
    /* 6. close the index and the file (flushes every dirty page) */
    err = close_files(spfd, amfd, err);
    IX_TimerStop(&t, &r->build);
    return err;
}

int IX_Method1(const IX_BuildSpec *spec, IX_BuildResult *r)
{
    IX_Timer t;
    IX_Measure open;
    long stored;
    int err;

    memset(r, 0, sizeof *r);
    /* setup (not index construction): the Student file must already exist */
    IX_TimerStart(&t);
    err = IX_LoadStudentFile(spec->data, spec->studentFile, spec->policy, &stored);
    IX_TimerStop(&t, &r->setup);
    if (err != IXE_OK)
        return err;
    err = IX_BuildFromExisting(spec->studentFile, spec->indexNo, spec->policy, r, &open);
    IX_MeasureAdd(&r->setup, &open);
    if (err == IXE_OK && r->recordsTotal != stored)
        return IXE_INVALID;
    return err;
}

/* ------------------------------------------------------------ Method 2 */

int IX_BuildIncremental(const IX_BuildSpec *spec, IX_BuildResult *r,
                        IX_ProgressFn progress, void *ctx, long every)
{
    const StudentData *d = spec->data;
    IX_Timer t;
    int spfd = -1, amfd = -1, rid, i, err;

    memset(r, 0, sizeof *r);
    IX_TimerStart(&t);
    /* 1-2. empty Student file and empty index */
    if ((err = SP_CreateFile((char *)spec->studentFile)) != RECE_OK)
        goto done;
    if ((spfd = SP_OpenFile((char *)spec->studentFile, spec->policy,
                            REC_SEARCH_CACHED)) < 0) {
        err = spfd;
        spfd = -1;
        goto done;
    }
    if ((amfd = create_and_open_index(spec->studentFile, spec->indexNo)) < 0) {
        err = amfd;
        amfd = -1;
        goto done;
    }
    /* 3-6. one record at a time: file first, then its index entry */
    for (i = 0; i < d->count; i++) {
        if ((err = SP_InsertRec(spfd, d->rec[i], d->len[i], &rid)) != RECE_OK)
            goto done;
        r->recordsTotal++;
        if ((err = index_record(amfd, d->rec[i], d->len[i], rid, r)) != IXE_OK)
            goto done;
        if (progress && every > 0 && r->recordsTotal % every == 0 &&
            (err = progress(ctx, spfd, amfd, r->recordsTotal)) < 0)
            goto done;
    }
    err = IXE_OK;

done:
    /* 7. close both files */
    err = close_files(spfd, amfd, err);
    IX_TimerStop(&t, &r->build);
    return err;
}

int IX_Method2(const IX_BuildSpec *spec, IX_BuildResult *r)
{
    return IX_BuildIncremental(spec, r, NULL, NULL, 0);
}
