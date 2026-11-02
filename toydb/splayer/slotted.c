/* slotted.c: slotted-page record files on top of the PF layer.
 * See slotted.h for the page layout and the design decisions it follows.
 *
 * PF usage rules followed by every function here:
 *   - a page is fixed only for the duration of one call and is unfixed on
 *     every return path, so even a 1-frame buffer pool is sufficient;
 *   - a modified page is reported once with PF_MarkDirty and then unfixed
 *     with dirty = FALSE, so each update is one logical write;
 *   - no pointer into a PF buffer is kept after the page is unfixed.
 */
#include <stdlib.h>
#include <string.h>
#include "slotted.h"

extern int PF_OpenFileWithPolicy(char *fname, int policy);

/* Per-open-file state kept in memory only. */
typedef struct SPfile {
    int used;
    int pffd;                  /* PF file descriptor                        */
    int search;                /* REC_SEARCH_CACHED or REC_SEARCH_SCAN       */
    int numPages;              /* data pages 0 .. numPages-1                 */
    int capacity;              /* allocated length of the two arrays below   */
    unsigned short *freeBytes; /* contiguous free bytes per page             */
    unsigned char *freeSlot;   /* page has an empty slot that can be reused  */
} SPfile;

static SPfile sptab[SP_MAX_FILES];

/* ---------------- byte-level page access ---------------- */

static unsigned get16(const char *page, int off)
{
    unsigned short v;
    memcpy(&v, page + off, 2);
    return v;
}

static void put16(char *page, int off, unsigned v)
{
    unsigned short s = (unsigned short)v;
    memcpy(page + off, &s, 2);
}

#define H_MAGIC 0
#define H_NSLOT 2
#define H_LIVE  4
#define H_FEND  6
#define SLOT_OFF(i) (SP_HEADER_SIZE + SP_SLOT_SIZE * (i))
#define SLOT_LEN(i) (SLOT_OFF(i) + 2)

static int free_start(const char *page)
{
    return SP_HEADER_SIZE + SP_SLOT_SIZE * (int)get16(page, H_NSLOT);
}

static void page_init(char *page)
{
    put16(page, H_MAGIC, SP_MAGIC);
    put16(page, H_NSLOT, 0);
    put16(page, H_LIVE, 0);
    put16(page, H_FEND, PF_PAGE_SIZE);
}

/* Checks that the header is self-consistent and that the live records
 * exactly fill [freeEnd, PF_PAGE_SIZE), i.e. the record area is compact. */
static int page_valid(const char *page)
{
    int n, live = 0, i;
    long bytes = 0;
    unsigned fend = get16(page, H_FEND);

    if (get16(page, H_MAGIC) != SP_MAGIC)
        return 0;
    n = (int)get16(page, H_NSLOT);
    if (free_start(page) > (int)fend || fend > PF_PAGE_SIZE)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned off = get16(page, SLOT_OFF(i)), len = get16(page, SLOT_LEN(i));
        if (off == 0)
            continue;
        if (len == 0 || off < fend || off + len > PF_PAGE_SIZE)
            return 0;
        live++;
        bytes += len;
    }
    return live == (int)get16(page, H_LIVE) && bytes == PF_PAGE_SIZE - (long)fend;
}

/* Lowest empty slot, or numSlots if the directory has none. */
static int first_empty_slot(const char *page)
{
    int n = (int)get16(page, H_NSLOT), i;
    for (i = 0; i < n; i++)
        if (get16(page, SLOT_OFF(i)) == 0)
            return i;
    return n;
}

/* Bytes an insert of `len` needs on this page (record + any new slot). */
static int space_needed(const char *page, int len)
{
    return len + (first_empty_slot(page) == (int)get16(page, H_NSLOT) ? SP_SLOT_SIZE : 0);
}

static int page_fits(const char *page, int len)
{
    return (int)get16(page, H_FEND) - free_start(page) >= space_needed(page, len);
}

/* Places a record on a page known to have room; returns its slot number. */
static int page_insert(char *page, const char *rec, int len)
{
    int slot = first_empty_slot(page);
    int n = (int)get16(page, H_NSLOT);
    int fend = (int)get16(page, H_FEND) - len;

    memcpy(page + fend, rec, len);
    put16(page, H_FEND, fend);
    put16(page, SLOT_OFF(slot), fend);
    put16(page, SLOT_LEN(slot), len);
    if (slot == n)
        put16(page, H_NSLOT, n + 1);
    put16(page, H_LIVE, get16(page, H_LIVE) + 1);
    return slot;
}

/* Removes the record in `slot` and compacts the record area immediately.
 * Records stored below the deleted one (lower offsets) slide up by its
 * length; their slots keep their numbers and only their offsets change. */
static void page_delete(char *page, int slot)
{
    int n = (int)get16(page, H_NSLOT), i;
    unsigned off = get16(page, SLOT_OFF(slot)), len = get16(page, SLOT_LEN(slot));
    unsigned fend = get16(page, H_FEND);

    memmove(page + fend + len, page + fend, off - fend);
    for (i = 0; i < n; i++) {
        unsigned o = get16(page, SLOT_OFF(i));
        if (o != 0 && o < off)
            put16(page, SLOT_OFF(i), o + len);
    }
    put16(page, H_FEND, fend + len);
    put16(page, SLOT_OFF(slot), 0);
    put16(page, SLOT_LEN(slot), 0);
    put16(page, H_LIVE, get16(page, H_LIVE) - 1);

    /* Empty slots at the end of the directory can be dropped without
     * renumbering any live slot; this returns their 4 bytes to free space. */
    while (n > 0 && get16(page, SLOT_OFF(n - 1)) == 0)
        n--;
    put16(page, H_NSLOT, n);
}

/* ---------------- file table helpers ---------------- */

static SPfile *get_file(int spfd)
{
    if (spfd < 0 || spfd >= SP_MAX_FILES || !sptab[spfd].used)
        return NULL;
    return &sptab[spfd];
}

/* Records a page's current free space in the in-memory cache. */
static int cache_page(SPfile *f, int pageNum, const char *page)
{
    if (pageNum >= f->capacity) {
        int cap = f->capacity ? f->capacity * 2 : 64;
        unsigned short *fb;
        unsigned char *fs;
        while (cap <= pageNum)
            cap *= 2;
        fb = realloc(f->freeBytes, cap * sizeof *fb);
        if (!fb)
            return RECE_NOMEM;
        f->freeBytes = fb;
        fs = realloc(f->freeSlot, cap * sizeof *fs);
        if (!fs)
            return RECE_NOMEM;
        f->freeSlot = fs;
        f->capacity = cap;
    }
    f->freeBytes[pageNum] = (unsigned short)(get16(page, H_FEND) - free_start(page));
    f->freeSlot[pageNum] = get16(page, H_NSLOT) > get16(page, H_LIVE);
    return RECE_OK;
}

/* Fetches a page, checking its number and contents; on error nothing is fixed. */
static int fetch(SPfile *f, int pageNum, char **page)
{
    int err;
    if (pageNum < 0 || pageNum >= f->numPages)
        return RECE_INVALIDRID;
    if ((err = PF_GetThisPage(f->pffd, pageNum, page)) != PFE_OK)
        return err;
    if (!page_valid(*page)) {
        PF_UnfixPage(f->pffd, pageNum, FALSE);
        return RECE_BADPAGE;
    }
    return RECE_OK;
}

/* Marks a modified page dirty, refreshes the cache and unfixes it. */
static int finish_update(SPfile *f, int pageNum, char *page)
{
    int err = PF_MarkDirty(f->pffd, pageNum);
    int err2 = cache_page(f, pageNum, page);
    int err3 = PF_UnfixPage(f->pffd, pageNum, FALSE);
    if (err != PFE_OK) return err;
    if (err2 != RECE_OK) return err2;
    return err3;
}

/* ---------------- public interface ---------------- */

int SP_CreateFile(char *fname) { return PF_CreateFile(fname); }
int SP_DestroyFile(char *fname) { return PF_DestroyFile(fname); }

int SP_OpenFile(char *fname, int policy, int search)
{
    int spfd, pffd, pageNum = -1, err;
    char *page;
    SPfile *f;

    if (search != REC_SEARCH_CACHED && search != REC_SEARCH_SCAN)
        return RECE_INVALIDARG;
    for (spfd = 0; spfd < SP_MAX_FILES && sptab[spfd].used; spfd++)
        ;
    if (spfd == SP_MAX_FILES)
        return RECE_FTABFULL;
    if ((pffd = PF_OpenFileWithPolicy(fname, policy)) < 0)
        return pffd;

    f = &sptab[spfd];
    memset(f, 0, sizeof *f);
    f->used = 1;
    f->pffd = pffd;
    f->search = search;

    /* One pass over the file validates every page, counts the pages and
     * builds the free-space cache.  Pages are never disposed, so they are
     * numbered 0 .. numPages-1 without gaps. */
    while ((err = PF_GetNextPage(pffd, &pageNum, &page)) == PFE_OK) {
        if (pageNum != f->numPages || !page_valid(page))
            err = RECE_BADPAGE;
        else
            err = cache_page(f, pageNum, page);
        PF_UnfixPage(pffd, pageNum, FALSE);
        if (err != RECE_OK)
            break;
        f->numPages++;
    }
    if (err != PFE_EOF) {
        SP_CloseFile(spfd);
        return err;
    }
    return spfd;
}

int SP_CloseFile(int spfd)
{
    SPfile *f = get_file(spfd);
    int err;
    if (!f)
        return RECE_FD;
    err = PF_CloseFile(f->pffd);
    free(f->freeBytes);
    free(f->freeSlot);
    memset(f, 0, sizeof *f);
    return err;
}

int SP_NumPages(int spfd)
{
    SPfile *f = get_file(spfd);
    return f ? f->numPages : RECE_FD;
}

int SP_InsertRec(int spfd, const char *rec, int len, int *rid)
{
    SPfile *f = get_file(spfd);
    int pageNum, slot, err;
    char *page = NULL;

    if (!f)
        return RECE_FD;
    if (!rec || !rid || len < 1 || len > SP_MAX_RECORD)
        return RECE_RECSIZE;

    /* First fit: the lowest-numbered page with enough contiguous room. */
    for (pageNum = 0; pageNum < f->numPages; pageNum++) {
        if (f->search == REC_SEARCH_CACHED) {
            int need = len + (f->freeSlot[pageNum] ? 0 : SP_SLOT_SIZE);
            if (f->freeBytes[pageNum] < need)
                continue;
            if ((err = fetch(f, pageNum, &page)) != RECE_OK)
                return err;
            if (!page_fits(page, len)) {       /* cache out of step: a bug */
                PF_UnfixPage(f->pffd, pageNum, FALSE);
                return RECE_BADPAGE;
            }
            break;
        }
        if ((err = fetch(f, pageNum, &page)) != RECE_OK)
            return err;
        if (page_fits(page, len))
            break;
        if ((err = PF_UnfixPage(f->pffd, pageNum, FALSE)) != PFE_OK)
            return err;
    }

    if (pageNum == f->numPages) {              /* no page fits: add one */
        int newPage;
        if (f->numPages >= REC_MAX_PAGES)
            return RECE_FILEFULL;
        if ((err = PF_AllocPage(f->pffd, &newPage, &page)) != PFE_OK)
            return err;
        if (newPage != f->numPages) {
            PF_UnfixPage(f->pffd, newPage, TRUE);
            return RECE_BADPAGE;
        }
        page_init(page);
        f->numPages++;
    }

    slot = page_insert(page, rec, len);
    if ((err = finish_update(f, pageNum, page)) != RECE_OK)
        return err;
    *rid = REC_MakeRid(pageNum, slot);
    return RECE_OK;
}

/* Fetches the page of `rid` and checks that its slot holds a record. */
static int fetch_record(SPfile *f, int rid, int *pageNum, int *slot, char **page)
{
    int err;
    if (REC_SplitRid(rid, pageNum, slot) != RECE_OK || *pageNum >= f->numPages)
        return RECE_INVALIDRID;
    if ((err = fetch(f, *pageNum, page)) != RECE_OK)
        return err;
    if (*slot >= (int)get16(*page, H_NSLOT) || get16(*page, SLOT_OFF(*slot)) == 0) {
        PF_UnfixPage(f->pffd, *pageNum, FALSE);
        return RECE_INVALIDRID;
    }
    return RECE_OK;
}

int SP_DeleteRec(int spfd, int rid)
{
    SPfile *f = get_file(spfd);
    int pageNum, slot, err;
    char *page;

    if (!f)
        return RECE_FD;
    if ((err = fetch_record(f, rid, &pageNum, &slot, &page)) != RECE_OK)
        return err;
    page_delete(page, slot);
    return finish_update(f, pageNum, page);
}

int SP_GetRec(int spfd, int rid, char *buf, int bufSize, int *len)
{
    SPfile *f = get_file(spfd);
    int pageNum, slot, err, n;
    char *page;

    if (!f)
        return RECE_FD;
    if (!len)
        return RECE_INVALIDARG;
    if ((err = fetch_record(f, rid, &pageNum, &slot, &page)) != RECE_OK)
        return err;
    n = (int)get16(page, SLOT_LEN(slot));
    *len = n;
    if (n > bufSize || !buf)
        err = RECE_BUFSMALL;
    else
        memcpy(buf, page + get16(page, SLOT_OFF(slot)), n);
    PF_UnfixPage(f->pffd, pageNum, FALSE);
    return err;
}

int SP_ScanOpen(int spfd, SP_Scan *scan)
{
    if (!get_file(spfd))
        return RECE_FD;
    if (!scan)
        return RECE_INVALIDARG;
    scan->spfd = spfd;
    scan->pageNum = 0;
    scan->slot = 0;
    scan->loaded = 0;
    scan->open = 1;
    return RECE_OK;
}

int SP_ScanNext(SP_Scan *scan, int *rid, char *buf, int bufSize, int *len)
{
    SPfile *f;
    int err;

    if (!scan || !scan->open || !(f = get_file(scan->spfd)))
        return RECE_FD;
    for (;;) {
        if (!scan->loaded) {
            char *page;
            if (scan->pageNum >= f->numPages)
                return RECE_EOF;
            if ((err = fetch(f, scan->pageNum, &page)) != RECE_OK)
                return err;
            memcpy(scan->page, page, PF_PAGE_SIZE);   /* one fetch per page */
            if ((err = PF_UnfixPage(f->pffd, scan->pageNum, FALSE)) != PFE_OK)
                return err;
            scan->loaded = 1;
            scan->slot = 0;
        }
        while (scan->slot < (int)get16(scan->page, H_NSLOT)) {
            int s = scan->slot++;
            unsigned off = get16(scan->page, SLOT_OFF(s));
            int n = (int)get16(scan->page, SLOT_LEN(s));
            if (off == 0)
                continue;                              /* deleted slot */
            if (rid)
                *rid = REC_MakeRid(scan->pageNum, s);
            if (len)
                *len = n;
            if (!buf || n > bufSize)
                return RECE_BUFSMALL;
            memcpy(buf, scan->page + off, n);
            return RECE_OK;
        }
        scan->pageNum++;
        scan->loaded = 0;
    }
}

int SP_ScanClose(SP_Scan *scan)
{
    if (!scan || !scan->open)
        return RECE_INVALIDARG;
    scan->open = 0;
    return RECE_OK;
}

int SP_GetSpaceStats(int spfd, REC_SpaceStats *s)
{
    SPfile *f = get_file(spfd);
    int pageNum, err;
    char *page;

    if (!f)
        return RECE_FD;
    if (!s)
        return RECE_INVALIDARG;
    memset(s, 0, sizeof *s);
    for (pageNum = 0; pageNum < f->numPages; pageNum++) {
        int fstart, fend;
        if ((err = fetch(f, pageNum, &page)) != RECE_OK)
            return err;
        fstart = free_start(page);
        fend = (int)get16(page, H_FEND);
        s->numPages++;
        s->numRecords += get16(page, H_LIVE);
        s->recordBytes += PF_PAGE_SIZE - fend;   /* compact record area */
        s->metadataBytes += fstart;              /* header + slot directory */
        s->freeBytes += fend - fstart;
        PF_UnfixPage(f->pffd, pageNum, FALSE);
    }
    return RECE_OK;
}

int SP_GetPageInfo(int spfd, int pageNum, SP_PageInfo *info)
{
    SPfile *f = get_file(spfd);
    int err;
    char *page;

    if (!f)
        return RECE_FD;
    if (!info)
        return RECE_INVALIDARG;
    if ((err = fetch(f, pageNum, &page)) != RECE_OK)
        return err;
    info->numSlots = (int)get16(page, H_NSLOT);
    info->liveCount = (int)get16(page, H_LIVE);
    info->freeStart = free_start(page);
    info->freeEnd = (int)get16(page, H_FEND);
    return PF_UnfixPage(f->pffd, pageNum, FALSE);
}
