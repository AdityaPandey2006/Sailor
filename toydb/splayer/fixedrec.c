/* fixedrec.c: fixed-length record files on top of PF.  See fixedrec.h.
 * Follows the same PF rules as slotted.c: one page fixed per call, unfixed
 * on every path, updates reported once via PF_MarkDirty. */
#include <stdlib.h>
#include <string.h>
#include "fixedrec.h"

extern int PF_OpenFileWithPolicy(char *fname, int policy);

typedef struct FXfile {
    int used, pffd, search, recLen, slots;
    int numPages, capacity;
    unsigned short *freeCount;  /* empty slots per page (in-memory cache) */
} FXfile;

static FXfile fxtab[FX_MAX_FILES];

static unsigned get16(const char *p, int off) { unsigned short v; memcpy(&v, p + off, 2); return v; }
static void put16(char *p, int off, unsigned v) { unsigned short s = (unsigned short)v; memcpy(p + off, &s, 2); }

#define H_MAGIC 0
#define H_LEN   2
#define H_CAP   4
#define H_LIVE  6
#define BITMAP_BYTES(n) (((n) + 7) / 8)

static int data_start(int slots) { return FX_HEADER_SIZE + BITMAP_BYTES(slots); }
static char *slot_ptr(char *page, int slots, int recLen, int i) { return page + data_start(slots) + i * recLen; }
static int bit_get(const char *page, int i) { return (page[FX_HEADER_SIZE + i / 8] >> (i % 8)) & 1; }
static void bit_set(char *page, int i, int on)
{
    unsigned char *b = (unsigned char *)page + FX_HEADER_SIZE + i / 8;
    if (on) *b |= (unsigned char)(1u << (i % 8));
    else    *b &= (unsigned char)~(1u << (i % 8));
}

int FX_SlotsPerPage(int recLen)
{
    int n;
    if (recLen < 1 || recLen > FX_MAX_RECLEN)
        return 0;
    n = (PF_PAGE_SIZE - FX_HEADER_SIZE) / recLen;          /* upper bound */
    while (n > 0 && data_start(n) + n * recLen > PF_PAGE_SIZE)
        n--;
    return n;
}

static FXfile *get_file(int fxfd)
{
    if (fxfd < 0 || fxfd >= FX_MAX_FILES || !fxtab[fxfd].used)
        return NULL;
    return &fxtab[fxfd];
}

static int page_valid(const FXfile *f, const char *page)
{
    int i, live = 0;
    if (get16(page, H_MAGIC) != FX_MAGIC || (int)get16(page, H_LEN) != f->recLen ||
        (int)get16(page, H_CAP) != f->slots)
        return 0;
    for (i = 0; i < f->slots; i++)
        live += bit_get(page, i);
    return live == (int)get16(page, H_LIVE);
}

static void page_init(const FXfile *f, char *page)
{
    memset(page, 0, PF_PAGE_SIZE);
    put16(page, H_MAGIC, FX_MAGIC);
    put16(page, H_LEN, f->recLen);
    put16(page, H_CAP, f->slots);
    put16(page, H_LIVE, 0);
}

static int cache_page(FXfile *f, int pageNum, const char *page)
{
    if (pageNum >= f->capacity) {
        int cap = f->capacity ? f->capacity * 2 : 64;
        unsigned short *fc;
        while (cap <= pageNum)
            cap *= 2;
        if (!(fc = realloc(f->freeCount, cap * sizeof *fc)))
            return RECE_NOMEM;
        f->freeCount = fc;
        f->capacity = cap;
    }
    f->freeCount[pageNum] = (unsigned short)(f->slots - (int)get16(page, H_LIVE));
    return RECE_OK;
}

static int fetch(FXfile *f, int pageNum, char **page)
{
    int err;
    if (pageNum < 0 || pageNum >= f->numPages)
        return RECE_INVALIDRID;
    if ((err = PF_GetThisPage(f->pffd, pageNum, page)) != PFE_OK)
        return err;
    if (!page_valid(f, *page)) {
        PF_UnfixPage(f->pffd, pageNum, FALSE);
        return RECE_BADPAGE;
    }
    return RECE_OK;
}

static int finish_update(FXfile *f, int pageNum, char *page)
{
    int err = PF_MarkDirty(f->pffd, pageNum);
    int err2 = cache_page(f, pageNum, page);
    int err3 = PF_UnfixPage(f->pffd, pageNum, FALSE);
    if (err != PFE_OK) return err;
    if (err2 != RECE_OK) return err2;
    return err3;
}

/* Length of the record stored in a slot (it ends at the first '\0' pad). */
static int stored_len(const char *slot, int recLen)
{
    const char *end = memchr(slot, '\0', recLen);
    return end ? (int)(end - slot) : recLen;
}

int FX_CreateFile(char *fname) { return PF_CreateFile(fname); }
int FX_DestroyFile(char *fname) { return PF_DestroyFile(fname); }

int FX_OpenFile(char *fname, int policy, int recLen, int search)
{
    int fxfd, pffd, pageNum = -1, err;
    char *page;
    FXfile *f;

    if (FX_SlotsPerPage(recLen) == 0 ||
        (search != REC_SEARCH_CACHED && search != REC_SEARCH_SCAN))
        return RECE_INVALIDARG;
    for (fxfd = 0; fxfd < FX_MAX_FILES && fxtab[fxfd].used; fxfd++)
        ;
    if (fxfd == FX_MAX_FILES)
        return RECE_FTABFULL;
    if ((pffd = PF_OpenFileWithPolicy(fname, policy)) < 0)
        return pffd;

    f = &fxtab[fxfd];
    memset(f, 0, sizeof *f);
    f->used = 1;
    f->pffd = pffd;
    f->search = search;
    f->recLen = recLen;
    f->slots = FX_SlotsPerPage(recLen);

    while ((err = PF_GetNextPage(pffd, &pageNum, &page)) == PFE_OK) {
        if (pageNum != f->numPages || !page_valid(f, page))
            err = RECE_BADPAGE;
        else
            err = cache_page(f, pageNum, page);
        PF_UnfixPage(pffd, pageNum, FALSE);
        if (err != RECE_OK)
            break;
        f->numPages++;
    }
    if (err != PFE_EOF) {
        FX_CloseFile(fxfd);
        return err;
    }
    return fxfd;
}

int FX_CloseFile(int fxfd)
{
    FXfile *f = get_file(fxfd);
    int err;
    if (!f)
        return RECE_FD;
    err = PF_CloseFile(f->pffd);
    free(f->freeCount);
    memset(f, 0, sizeof *f);
    return err;
}

int FX_NumPages(int fxfd)
{
    FXfile *f = get_file(fxfd);
    return f ? f->numPages : RECE_FD;
}

int FX_InsertRec(int fxfd, const char *rec, int len, int *rid)
{
    FXfile *f = get_file(fxfd);
    int pageNum, slot, err;
    char *page = NULL;

    if (!f)
        return RECE_FD;
    if (!rec || !rid || len < 1 || len > f->recLen)
        return RECE_RECSIZE;                       /* rejected, not truncated */
    if (memchr(rec, '\0', len))
        return RECE_INVALIDARG;                    /* '\0' is the pad byte */

    for (pageNum = 0; pageNum < f->numPages; pageNum++) {
        if (f->search == REC_SEARCH_CACHED && f->freeCount[pageNum] == 0)
            continue;
        if ((err = fetch(f, pageNum, &page)) != RECE_OK)
            return err;
        if ((int)get16(page, H_LIVE) < f->slots)
            break;
        if (f->search == REC_SEARCH_CACHED) {      /* cache out of step: a bug */
            PF_UnfixPage(f->pffd, pageNum, FALSE);
            return RECE_BADPAGE;
        }
        if ((err = PF_UnfixPage(f->pffd, pageNum, FALSE)) != PFE_OK)
            return err;
    }

    if (pageNum == f->numPages) {
        int newPage;
        if (f->numPages >= REC_MAX_PAGES)
            return RECE_FILEFULL;
        if ((err = PF_AllocPage(f->pffd, &newPage, &page)) != PFE_OK)
            return err;
        if (newPage != f->numPages) {
            PF_UnfixPage(f->pffd, newPage, TRUE);
            return RECE_BADPAGE;
        }
        page_init(f, page);
        f->numPages++;
    }

    for (slot = 0; bit_get(page, slot); slot++)
        ;
    {
        char *dst = slot_ptr(page, f->slots, f->recLen, slot);
        memcpy(dst, rec, len);
        memset(dst + len, 0, f->recLen - len);     /* padding */
    }
    bit_set(page, slot, 1);
    put16(page, H_LIVE, get16(page, H_LIVE) + 1);
    if ((err = finish_update(f, pageNum, page)) != RECE_OK)
        return err;
    *rid = REC_MakeRid(pageNum, slot);
    return RECE_OK;
}

static int fetch_record(FXfile *f, int rid, int *pageNum, int *slot, char **page)
{
    int err;
    if (REC_SplitRid(rid, pageNum, slot) != RECE_OK || *pageNum >= f->numPages ||
        *slot >= f->slots)
        return RECE_INVALIDRID;
    if ((err = fetch(f, *pageNum, page)) != RECE_OK)
        return err;
    if (!bit_get(*page, *slot)) {
        PF_UnfixPage(f->pffd, *pageNum, FALSE);
        return RECE_INVALIDRID;
    }
    return RECE_OK;
}

int FX_DeleteRec(int fxfd, int rid)
{
    FXfile *f = get_file(fxfd);
    int pageNum, slot, err;
    char *page;

    if (!f)
        return RECE_FD;
    if ((err = fetch_record(f, rid, &pageNum, &slot, &page)) != RECE_OK)
        return err;
    bit_set(page, slot, 0);
    memset(slot_ptr(page, f->slots, f->recLen, slot), 0, f->recLen);
    put16(page, H_LIVE, get16(page, H_LIVE) - 1);
    return finish_update(f, pageNum, page);
}

int FX_GetRec(int fxfd, int rid, char *buf, int bufSize, int *len)
{
    FXfile *f = get_file(fxfd);
    int pageNum, slot, err, n;
    char *page, *src;

    if (!f)
        return RECE_FD;
    if (!len)
        return RECE_INVALIDARG;
    if ((err = fetch_record(f, rid, &pageNum, &slot, &page)) != RECE_OK)
        return err;
    src = slot_ptr(page, f->slots, f->recLen, slot);
    n = stored_len(src, f->recLen);
    *len = n;
    if (!buf || n > bufSize)
        err = RECE_BUFSMALL;
    else
        memcpy(buf, src, n);
    PF_UnfixPage(f->pffd, pageNum, FALSE);
    return err;
}

int FX_ScanOpen(int fxfd, FX_Scan *scan)
{
    if (!get_file(fxfd))
        return RECE_FD;
    if (!scan)
        return RECE_INVALIDARG;
    scan->fxfd = fxfd;
    scan->pageNum = scan->slot = scan->loaded = 0;
    scan->open = 1;
    return RECE_OK;
}

int FX_ScanNext(FX_Scan *scan, int *rid, char *buf, int bufSize, int *len)
{
    FXfile *f;
    int err;

    if (!scan || !scan->open || !(f = get_file(scan->fxfd)))
        return RECE_FD;
    for (;;) {
        if (!scan->loaded) {
            char *page;
            if (scan->pageNum >= f->numPages)
                return RECE_EOF;
            if ((err = fetch(f, scan->pageNum, &page)) != RECE_OK)
                return err;
            memcpy(scan->page, page, PF_PAGE_SIZE);
            if ((err = PF_UnfixPage(f->pffd, scan->pageNum, FALSE)) != PFE_OK)
                return err;
            scan->loaded = 1;
            scan->slot = 0;
        }
        while (scan->slot < f->slots) {
            int s = scan->slot++, n;
            char *src;
            if (!bit_get(scan->page, s))
                continue;
            src = slot_ptr(scan->page, f->slots, f->recLen, s);
            n = stored_len(src, f->recLen);
            if (rid) *rid = REC_MakeRid(scan->pageNum, s);
            if (len) *len = n;
            if (!buf || n > bufSize)
                return RECE_BUFSMALL;
            memcpy(buf, src, n);
            return RECE_OK;
        }
        scan->pageNum++;
        scan->loaded = 0;
    }
}

int FX_ScanClose(FX_Scan *scan)
{
    if (!scan || !scan->open)
        return RECE_INVALIDARG;
    scan->open = 0;
    return RECE_OK;
}

int FX_GetSpaceStats(int fxfd, REC_SpaceStats *s)
{
    FXfile *f = get_file(fxfd);
    int pageNum, i, err;
    char *page;

    if (!f)
        return RECE_FD;
    if (!s)
        return RECE_INVALIDARG;
    memset(s, 0, sizeof *s);
    for (pageNum = 0; pageNum < f->numPages; pageNum++) {
        if ((err = fetch(f, pageNum, &page)) != RECE_OK)
            return err;
        s->numPages++;
        s->metadataBytes += data_start(f->slots);
        /* tail of the page too small to hold another slot */
        s->paddingBytes += PF_PAGE_SIZE - data_start(f->slots) - f->slots * f->recLen;
        for (i = 0; i < f->slots; i++) {
            if (bit_get(page, i)) {
                int n = stored_len(slot_ptr(page, f->slots, f->recLen, i), f->recLen);
                s->numRecords++;
                s->recordBytes += n;
                s->paddingBytes += f->recLen - n;
            } else {
                s->freeBytes += f->recLen;
            }
        }
        PF_UnfixPage(f->pffd, pageNum, FALSE);
    }
    return RECE_OK;
}
