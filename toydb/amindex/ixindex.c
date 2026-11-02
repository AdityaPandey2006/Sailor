/* ixindex.c: keys, measurement, queries, tree inspection and validation
 * shared by every index-construction method.  See ixindex.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "amwrap.h"
#include "slotted.h"
#include "ixindex.h"

/* ================================================================ keys */

int IX_ExtractKey(const char *rec, int len, char key[IX_KEY_LEN])
{
    int i = 0, field = 0, start, n;

    while (field < IX_KEY_FIELD && i < len)
        if (rec[i++] == ';')
            field++;
    if (field < IX_KEY_FIELD)
        return IXE_NOKEY;
    start = i;
    while (i < len && rec[i] != ';')
        i++;
    n = i - start;
    if (n <= 0 || n > IX_KEY_LEN)
        return IXE_NOKEY;
    memset(key, 0, IX_KEY_LEN);
    memcpy(key, rec + start, n);
    return IXE_OK;
}

const char *IX_KeyString(const char key[IX_KEY_LEN], char out[IX_KEY_LEN + 1])
{
    memcpy(out, key, IX_KEY_LEN);
    out[IX_KEY_LEN] = '\0';
    return out;
}

void IX_IndexFileName(const char *studentFile, int indexNo, char *out, int outSize)
{
    snprintf(out, outSize, "%s.%d", studentFile, indexNo);
}

void IX_RemoveFiles(const char *studentFile, int indexNo)
{
    char name[1024];
    IX_IndexFileName(studentFile, indexNo, name, sizeof name);
    unlink(studentFile);
    unlink(name);
}

const char *IX_ErrorString(int code)
{
    static const char *am[] = {
        "ok", "AM: invalid attribute length", "AM: key not found", "AM: PF error",
        "AM: internal error", "AM: invalid scan descriptor",
        "AM: invalid scan operator", "AM: end of scan", "AM: scan table full",
        "AM: invalid attribute type", "AM: invalid file descriptor",
        "AM: invalid value"
    };
    switch (code) {
    case IXE_OK:      return "ok";
    case IXE_NOKEY:   return "record has no usable roll-no";
    case IXE_INVALID: return "index failed validation";
    case IXE_NOMEM:   return "out of memory";
    case IXE_IO:      return "file I/O error";
    }
    if (code <= -101 && code >= -110)
        return REC_ErrorString(code);
    if (code < 0 && code >= -11)
        return am[-code];          /* AM and PF codes overlap; AM wins here */
    return "PF/unknown error";
}

/* ========================================================== measuring */

double IX_Now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void IX_TimerStart(IX_Timer *t)
{
    t->s0 = PF_GetStats();
    t->t0 = IX_Now();
}

void IX_TimerStop(const IX_Timer *t, IX_Measure *m)
{
    double t1 = IX_Now();
    PF_Stats s1 = PF_GetStats();

    m->seconds = t1 - t->t0;
    m->pf.logical_reads = s1.logical_reads - t->s0.logical_reads;
    m->pf.logical_writes = s1.logical_writes - t->s0.logical_writes;
    m->pf.physical_reads = s1.physical_reads - t->s0.physical_reads;
    m->pf.physical_writes = s1.physical_writes - t->s0.physical_writes;
    m->pf.buffer_hits = s1.buffer_hits - t->s0.buffer_hits;
    m->pf.buffer_misses = s1.buffer_misses - t->s0.buffer_misses;
}

void IX_MeasureAdd(IX_Measure *sum, const IX_Measure *m)
{
    sum->seconds += m->seconds;
    sum->pf.logical_reads += m->pf.logical_reads;
    sum->pf.logical_writes += m->pf.logical_writes;
    sum->pf.physical_reads += m->pf.physical_reads;
    sum->pf.physical_writes += m->pf.physical_writes;
    sum->pf.buffer_hits += m->pf.buffer_hits;
    sum->pf.buffer_misses += m->pf.buffer_misses;
}

/* ===================================================== dataset keys */

static int key_cmp(const void *a, const void *b)
{
    return memcmp(a, b, IX_KEY_LEN);
}

/* Sorted array of the usable roll-nos of d (duplicates kept). */
static int sorted_keys(const StudentData *d, char (**out)[IX_KEY_LEN], long *n)
{
    char (*k)[IX_KEY_LEN] = malloc((d->count ? d->count : 1) * sizeof *k);
    long i, m = 0;

    if (!k)
        return IXE_NOMEM;
    for (i = 0; i < d->count; i++)
        if (IX_ExtractKey(d->rec[i], d->len[i], k[m]) == IXE_OK)
            m++;
    qsort(k, m, sizeof *k, key_cmp);
    *out = k;
    *n = m;
    return IXE_OK;
}

static int key_present(char (*k)[IX_KEY_LEN], long n, const char *key)
{
    return bsearch(key, k, n, sizeof *k, key_cmp) != NULL;
}

/* Deterministic 32-bit LCG (same constants as the part-2 tests). */
static unsigned lcg_next(unsigned long *s)
{
    *s = (1664525UL * *s + 1013904223UL) & 0xffffffffUL;
    return (unsigned)(*s >> 8);
}

/* An absent key that sorts among the real ones: a real roll-no whose last
 * character is replaced by a letter (roll-nos end in digits).  Falls back
 * to "~" keys, which sort after every real key. */
static void absent_key(char (*k)[IX_KEY_LEN], long n, unsigned long *s, char *out)
{
    int tries;
    for (tries = 0; n > 0 && tries < 100; tries++) {
        int last;
        memcpy(out, k[lcg_next(s) % n], IX_KEY_LEN);
        for (last = IX_KEY_LEN - 1; last > 0 && out[last] == '\0'; last--)
            ;
        out[last] = (char)('A' + lcg_next(s) % 26);
        if (!key_present(k, n, out))
            return;
    }
    memset(out, '~', IX_KEY_LEN);
    out[IX_KEY_LEN - 1] = (char)('A' + lcg_next(s) % 26);
}

/* ============================================================ queries */

int IX_OpenScan(int amfd, int op, char *key)
{
    return AM_OpenIndexScan(amfd, IX_KEY_TYPE, IX_KEY_LEN, op, key);
}


int IX_MakeQueries(const StudentData *d, int hits, int misses, unsigned seed,
                   IX_QuerySet *q)
{
    char (*k)[IX_KEY_LEN];
    long n, i;
    unsigned long s = seed;
    int err;

    memset(q, 0, sizeof *q);
    if (hits < 0 || misses < 0)
        return IXE_INVALID;
    if ((err = sorted_keys(d, &k, &n)) != IXE_OK)
        return err;
    if (n == 0)
        hits = 0;
    q->n = hits + misses;
    if (!(q->key = malloc((q->n ? q->n : 1) * sizeof *q->key))) {
        free(k);
        return IXE_NOMEM;
    }
    for (i = 0; i < hits; i++)
        memcpy(q->key[i], k[lcg_next(&s) % n], IX_KEY_LEN);
    for (; i < q->n; i++)
        absent_key(k, n, &s, q->key[i]);
    /* Fisher-Yates shuffle so hits and misses interleave */
    for (i = q->n - 1; i > 0; i--) {
        long j = lcg_next(&s) % (i + 1);
        char t[IX_KEY_LEN];
        memcpy(t, q->key[i], IX_KEY_LEN);
        memcpy(q->key[i], q->key[j], IX_KEY_LEN);
        memcpy(q->key[j], t, IX_KEY_LEN);
    }
    free(k);
    return IXE_OK;
}

int IX_SaveQueries(const char *path, const IX_QuerySet *q)
{
    FILE *fp = fopen(path, "w");
    char s[IX_KEY_LEN + 1];
    int i;

    if (!fp)
        return IXE_IO;
    for (i = 0; i < q->n; i++)
        fprintf(fp, "%s\n", IX_KeyString(q->key[i], s));
    return fclose(fp) == 0 ? IXE_OK : IXE_IO;
}

int IX_LoadQueries(const char *path, IX_QuerySet *q)
{
    FILE *fp = fopen(path, "r");
    char line[256];
    int cap = 0;

    memset(q, 0, sizeof *q);
    if (!fp)
        return IXE_IO;
    while (fgets(line, sizeof line, fp)) {
        int n = (int)strcspn(line, "\r\n");
        if (n == 0)
            continue;
        if (n > IX_KEY_LEN) {
            fclose(fp);
            IX_FreeQueries(q);
            return IXE_IO;
        }
        if (q->n == cap) {
            void *p = realloc(q->key, (cap = cap ? cap * 2 : 1024) * sizeof *q->key);
            if (!p) {
                fclose(fp);
                IX_FreeQueries(q);
                return IXE_NOMEM;
            }
            q->key = p;
        }
        memset(q->key[q->n], 0, IX_KEY_LEN);
        memcpy(q->key[q->n], line, n);
        q->n++;
    }
    fclose(fp);
    return IXE_OK;
}

void IX_FreeQueries(IX_QuerySet *q)
{
    free(q->key);
    memset(q, 0, sizeof *q);
}

int IX_RunQueriesFd(int amfd, const IX_QuerySet *q, IX_QueryResult *r)
{
    IX_Timer t;
    int i, err = IXE_OK;

    memset(r, 0, sizeof *r);
    IX_TimerStart(&t);
    for (i = 0; i < q->n && err == IXE_OK; i++) {
        int sd = IX_OpenScan(amfd, EQUAL, q->key[i]);
        int rid;
        long got = 0;

        if (sd < 0) {
            err = sd;
            break;
        }
        while ((rid = AM_FindNextEntry(sd)) >= 0) {
            got++;
            r->ridSum = r->ridSum * 1000003ULL + (unsigned)rid;
        }
        if (rid != AME_EOF)
            err = rid;
        AM_CloseIndexScan(sd);
        r->queries++;
        r->rids += got;
        if (got)
            r->found++;
        else
            r->notFound++;
    }
    IX_TimerStop(&t, &r->m);
    return err;
}

int IX_RunQueries(const char *studentFile, int indexNo, const IX_QuerySet *q,
                  IX_QueryResult *r)
{
    char name[1024];
    int fd, err, cerr;

    IX_IndexFileName(studentFile, indexNo, name, sizeof name);
    if ((fd = PF_OpenFile(name)) < 0)
        return fd;
    err = IX_RunQueriesFd(fd, q, r);
    cerr = PF_CloseFile(fd);
    return err != IXE_OK ? err : cerr;
}

/* ======================================================= inspection */

typedef struct Walk {
    int fd;
    IX_TreeInfo *t;
    int leafDepth;
    int *leaves;
    long nLeaves, capLeaves;
    double fillSum;
} Walk;

#define WALK_FAIL(...) do { fprintf(stderr, "index invalid: " __VA_ARGS__); \
                            fputc('\n', stderr); return IXE_INVALID; } while (0)

static int read_page(int fd, int pageNum, char *copy)
{
    char *buf;
    int err = PF_GetThisPage(fd, pageNum, &buf);
    if (err != PFE_OK)
        return err;
    memcpy(copy, buf, PF_PAGE_SIZE);
    return PF_UnfixPage(fd, pageNum, FALSE);
}

/* lo (inclusive) and hi (exclusive) are separator bounds or NULL. */
static int walk(Walk *w, int pageNum, int depth, const char *lo, const char *hi)
{
    char page[PF_PAGE_SIZE];
    int err, i;

    if (depth > 64)
        WALK_FAIL("tree deeper than 64 levels at page %d", pageNum);
    if ((err = read_page(w->fd, pageNum, page)) != PFE_OK)
        return err;

    if (page[0] == 'l') {
        AM_LEAFHEADER h;
        int recSize = IX_KEY_LEN + AM_ss;
        long used = AM_sl;

        memcpy(&h, page, AM_sl);
        if (h.attrLength != IX_KEY_LEN)
            WALK_FAIL("leaf %d attrLength %d", pageNum, h.attrLength);
        if (h.numKeys < 0 || h.keyPtr != (int)AM_sl + h.numKeys * recSize ||
            h.recIdPtr < h.keyPtr || h.recIdPtr > PF_PAGE_SIZE)
            WALK_FAIL("leaf %d has a corrupt header", pageNum);
        if (w->leafDepth == 0)
            w->leafDepth = depth;
        else if (w->leafDepth != depth)
            WALK_FAIL("leaves at depths %d and %d", w->leafDepth, depth);
        for (i = 0; i < h.numKeys; i++) {
            const char *key = page + AM_sl + i * recSize;
            short p;
            long cnt = 0;

            if (i > 0 && memcmp(key - recSize, key, IX_KEY_LEN) >= 0)
                WALK_FAIL("leaf %d keys out of order at %d", pageNum, i);
            if ((lo && memcmp(key, lo, IX_KEY_LEN) < 0) ||
                (hi && memcmp(key, hi, IX_KEY_LEN) >= 0))
                WALK_FAIL("leaf %d key %d outside its separator range", pageNum, i);
            memcpy(&p, key + IX_KEY_LEN, AM_ss);
            while (p != AM_NULL) {
                int rid;
                if (p < h.recIdPtr || p > PF_PAGE_SIZE - (int)(AM_si + AM_ss) ||
                    ++cnt > PF_PAGE_SIZE / (long)(AM_si + AM_ss))
                    WALK_FAIL("leaf %d has a corrupt RID list", pageNum);
                memcpy(&rid, page + p, AM_si);
                if (rid < 0)
                    WALK_FAIL("leaf %d holds negative RID %d", pageNum, rid);
                memcpy(&p, page + p + AM_si, AM_ss);
            }
            if (cnt == 0)
                WALK_FAIL("leaf %d key %d has no RIDs", pageNum, i);
            w->t->entries += cnt;
            used += recSize + cnt * (AM_si + AM_ss);
        }
        w->t->keys += h.numKeys;
        w->t->leafPages++;
        w->fillSum += (double)used / PF_PAGE_SIZE;
        if (w->nLeaves == w->capLeaves) {
            int *p = realloc(w->leaves, (w->capLeaves = w->capLeaves ? 2 * w->capLeaves
                                                                       : 256) * sizeof *p);
            if (!p)
                return IXE_NOMEM;
            w->leaves = p;
        }
        w->leaves[w->nLeaves++] = pageNum;
        if (depth > w->t->height)
            w->t->height = depth;
        return IXE_OK;
    }

    if (page[0] == 'i') {
        AM_INTHEADER h;
        int recSize = AM_si + IX_KEY_LEN;

        memcpy(&h, page, AM_sint);
        if (h.attrLength != IX_KEY_LEN)
            WALK_FAIL("internal node %d attrLength %d", pageNum, h.attrLength);
        if (h.numKeys < 1 || h.numKeys > h.maxKeys)
            WALK_FAIL("internal node %d has %d keys (max %d)", pageNum,
                      h.numKeys, h.maxKeys);
        w->t->internalPages++;
        for (i = 0; i < h.numKeys; i++) {
            const char *key = page + AM_sint + AM_si + i * recSize;
            if (i > 0 && memcmp(key - recSize, key, IX_KEY_LEN) >= 0)
                WALK_FAIL("internal node %d keys out of order", pageNum);
            if ((lo && memcmp(key, lo, IX_KEY_LEN) < 0) ||
                (hi && memcmp(key, hi, IX_KEY_LEN) >= 0))
                WALK_FAIL("internal node %d key outside its range", pageNum);
        }
        for (i = 0; i <= h.numKeys; i++) {
            int child;
            char clo[IX_KEY_LEN], chi[IX_KEY_LEN];
            const char *l = lo, *u = hi;

            memcpy(&child, page + AM_sint + i * recSize, AM_si);
            if (i > 0) {
                memcpy(clo, page + AM_sint + AM_si + (i - 1) * recSize, IX_KEY_LEN);
                l = clo;
            }
            if (i < h.numKeys) {
                memcpy(chi, page + AM_sint + AM_si + i * recSize, IX_KEY_LEN);
                u = chi;
            }
            if ((err = walk(w, child, depth + 1, l, u)) != IXE_OK)
                return err;
        }
        return IXE_OK;
    }
    WALK_FAIL("page %d has unknown type '%c'", pageNum, page[0]);
}

int IX_InspectTreeFd(int fd, IX_TreeInfo *t)
{
    Walk w;
    char page[PF_PAGE_SIZE];
    int err, rootPage;
    long i;
    char *buf;

    memset(t, 0, sizeof *t);
    memset(&w, 0, sizeof w);
    w.fd = fd;
    w.t = t;

    /* AM always keeps the root in the first page of the file */
    if ((err = PF_GetFirstPage(fd, &rootPage, &buf)) != PFE_OK)
        return err;
    memcpy(page, buf, PF_PAGE_SIZE);
    if ((err = PF_UnfixPage(fd, rootPage, FALSE)) != PFE_OK)
        return err;
    if (page[0] == 'l') {
        AM_LEAFHEADER h;
        memcpy(&h, page, AM_sl);
        t->rootMaxKeys = h.maxKeys;
    } else {
        AM_INTHEADER h;
        memcpy(&h, page, AM_sint);
        t->rootMaxKeys = h.maxKeys;
    }

    err = walk(&w, rootPage, 1, NULL, NULL);

    /* the leaf chain must visit the leaves in key order and then stop */
    for (i = 0; err == IXE_OK && i < w.nLeaves; i++) {
        AM_LEAFHEADER h;
        if ((err = read_page(fd, w.leaves[i], page)) != PFE_OK)
            break;
        memcpy(&h, page, AM_sl);
        if (h.nextLeafPage != (i + 1 < w.nLeaves ? w.leaves[i + 1] : AM_NULL_PAGE)) {
            fprintf(stderr, "index invalid: leaf chain breaks after page %d\n",
                    w.leaves[i]);
            err = IXE_INVALID;
        }
    }
    if (err == IXE_OK && t->height > 1 && w.nLeaves > 0 && w.leaves[0] != 2) {
        /* AM_OpenIndexScan assumes the leftmost leaf is page 2 */
        fprintf(stderr, "index invalid: leftmost leaf is page %d, not 2\n", w.leaves[0]);
        err = IXE_INVALID;
    }
    if (t->leafPages)
        t->leafFill = w.fillSum / t->leafPages;
    free(w.leaves);
    return err;
}

int IX_InspectTree(const char *studentFile, int indexNo, IX_TreeInfo *t)
{
    char name[1024];
    int fd, err, cerr;

    IX_IndexFileName(studentFile, indexNo, name, sizeof name);
    if ((fd = PF_OpenFile(name)) < 0)
        return fd;
    err = IX_InspectTreeFd(fd, t);
    cerr = PF_CloseFile(fd);
    return err != IXE_OK ? err : cerr;
}

/* ======================================================== validation */

#define VERR(code, ...) do { err = (code); \
        fprintf(stderr, "validation error %d (PFerrno %d): ", err, PFerrno); \
        fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); goto done; } while (0)
#define VFAIL(...) do { fprintf(stderr, "validation failed: " __VA_ARGS__); \
                        fputc('\n', stderr); err = IXE_INVALID; goto done; } while (0)

static int int_cmp(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/* Fetches the record named by rid and extracts its key. */
static int record_key(int spfd, int rid, char *key)
{
    char rec[SP_MAX_RECORD + 1];
    int len, err = SP_GetRec(spfd, rid, rec, sizeof rec, &len);
    if (err != RECE_OK)
        return err;
    return IX_ExtractKey(rec, len, key);
}

int IX_Validate(const char *studentFile, int indexNo, const StudentData *d,
                IX_ValidateResult *v)
{
    char name[1024], ks[IX_KEY_LEN + 1];
    char (*k)[IX_KEY_LEN] = NULL;
    long n, i, j;
    int *rids = NULL;
    int spfd = -1, amfd = -1, sd = -1, err, rid;
    unsigned long s = 99991;

    memset(v, 0, sizeof *v);
    if ((err = sorted_keys(d, &k, &n)) != IXE_OK)
        return err;
    v->expectedEntries = n;
    if (!(rids = malloc((n + 1) * sizeof *rids))) {
        err = IXE_NOMEM;
        goto done;
    }
    IX_IndexFileName(studentFile, indexNo, name, sizeof name);
    if ((spfd = SP_OpenFile((char *)studentFile, PF_POLICY_LRU, REC_SEARCH_SCAN)) < 0) {
        err = spfd;
        goto done;
    }
    if ((amfd = PF_OpenFile(name)) < 0) {
        err = amfd;
        goto done;
    }

    /* 1. full scan: right number of entries, in key order, valid RIDs */
    {
        char prev[IX_KEY_LEN], key[IX_KEY_LEN];
        if ((sd = IX_OpenScan(amfd, EQUAL, NULL)) < 0)
            VERR(sd, "opening the full scan");
        while ((rid = AM_FindNextEntry(sd)) >= 0) {
            if ((err = record_key(spfd, rid, key)) != IXE_OK)
                VFAIL("full scan returned RID %d: %s", rid, IX_ErrorString(err));
            if (v->scannedEntries > 0 && memcmp(prev, key, IX_KEY_LEN) > 0)
                VFAIL("full scan out of key order at entry %ld", v->scannedEntries);
            memcpy(prev, key, IX_KEY_LEN);
            if (++v->scannedEntries > n)
                VFAIL("full scan returned more than %ld entries", n);
        }
        if (rid != AME_EOF)
            VERR(rid, "full scan after %ld entries", v->scannedEntries);
        AM_CloseIndexScan(sd);
        sd = -1;
        if (v->scannedEntries != n)
            VFAIL("full scan returned %ld entries, expected %ld", v->scannedEntries, n);
    }

    /* 2. every distinct key: exact RID multiset */
    for (i = 0; i < n; i = j) {
        long cnt = 0, want, x;
        for (j = i; j < n && memcmp(k[i], k[j], IX_KEY_LEN) == 0; j++)
            ;
        want = j - i;
        v->distinctKeys++;
        if (want > 1)
            v->duplicateKeys++;
        if ((sd = IX_OpenScan(amfd, EQUAL, k[i])) < 0)
            VERR(sd, "opening scan for key %s", IX_KeyString(k[i], ks));
        while ((rid = AM_FindNextEntry(sd)) >= 0) {
            char key[IX_KEY_LEN];
            if (cnt == want)
                VFAIL("key %s: more than %ld RIDs", IX_KeyString(k[i], ks), want);
            if ((err = record_key(spfd, rid, key)) != IXE_OK)
                VFAIL("key %s: RID %d: %s", IX_KeyString(k[i], ks), rid,
                      IX_ErrorString(err));
            if (memcmp(key, k[i], IX_KEY_LEN) != 0)
                VFAIL("key %s: RID %d points to another roll-no",
                      IX_KeyString(k[i], ks), rid);
            rids[cnt++] = rid;
        }
        if (rid != AME_EOF)
            VERR(rid, "scan for key %s after %ld RIDs", IX_KeyString(k[i], ks), cnt);
        AM_CloseIndexScan(sd);
        sd = -1;
        if (cnt != want)
            VFAIL("key %s: %ld RIDs, expected %ld", IX_KeyString(k[i], ks), cnt, want);
        qsort(rids, cnt, sizeof *rids, int_cmp);
        for (x = 1; x < cnt; x++)
            if (rids[x] == rids[x - 1])
                VFAIL("key %s: RID %d returned twice", IX_KeyString(k[i], ks), rids[x]);
        v->keysChecked++;
    }

    /* 3. absent keys: below, between and above the real ones */
    for (i = 0; i < 202; i++) {
        char key[IX_KEY_LEN];
        if (i == 0)
            memset(key, 0, IX_KEY_LEN);           /* the empty key */
        else if (i == 1)
            memset(key, '~', IX_KEY_LEN);         /* above everything */
        else
            absent_key(k, n, &s, key);
        if ((sd = IX_OpenScan(amfd, EQUAL, key)) < 0)
            VERR(sd, "opening scan for absent key %s", IX_KeyString(key, ks));
        rid = AM_FindNextEntry(sd);
        AM_CloseIndexScan(sd);
        sd = -1;
        if (rid != AME_EOF)
            VFAIL("absent key %s returned %d", IX_KeyString(key, ks), rid);
        v->absentChecked++;
    }
    err = IXE_OK;

done:
    if (sd >= 0)
        AM_CloseIndexScan(sd);
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
    free(rids);
    free(k);
    return err;
}
