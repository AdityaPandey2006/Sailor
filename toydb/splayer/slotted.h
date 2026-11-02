/* slotted.h: variable-length records stored in slotted pages on top of PF.
 *
 * Page layout (all PF_PAGE_SIZE = 4096 bytes of the PF data area; the PF
 * layer keeps its own per-page header outside this area).  Every field is an
 * unsigned 16-bit integer at a fixed byte offset - no pointers are stored.
 *
 *   offset 0  magic      SP_MAGIC, identifies a slotted page
 *   offset 2  numSlots   entries in the slot directory (live + empty)
 *   offset 4  liveCount  slots that currently hold a record
 *   offset 6  freeEnd    first byte of the record area (4096 when empty)
 *   offset 8  slot directory, 4 bytes per slot: {offset, length}
 *             an empty (deleted) slot has offset 0
 *
 *   [header 8B][slot 0][slot 1]...[slot n-1] free space [rec][rec]...[rec]
 *                                ^freeStart = 8 + 4*n  ^freeEnd        ^4096
 *
 * The directory grows forward and the record area grows backward from the
 * end of the page.  The record area is always contiguous: deleting a record
 * compacts the page immediately (decision 6), moving the other records and
 * updating their offsets while slot numbers - and therefore RIDs - stay the
 * same.  A deleted slot is reused by the next insert into that page
 * (decision 2).  Inserts go to the first page with enough room (decision 5).
 *
 * Records are opaque byte strings of 1..SP_MAX_RECORD bytes (decision 1:
 * the loader stores each student.txt line unchanged).
 */
#ifndef TOYDB_SLOTTED_H
#define TOYDB_SLOTTED_H

#include "recfile.h"

#define SP_MAGIC        0x5350                       /* "SP" */
#define SP_HEADER_SIZE  8
#define SP_SLOT_SIZE    4
#define SP_MAX_RECORD   (PF_PAGE_SIZE - SP_HEADER_SIZE - SP_SLOT_SIZE)  /* 4084 */
#define SP_MAX_FILES    20

/* Snapshot of one page's header, for tests and diagnostics. */
typedef struct SP_PageInfo {
    int numSlots, liveCount;
    int freeStart, freeEnd;    /* contiguous free space is [freeStart, freeEnd) */
} SP_PageInfo;

/* Sequential scan state.  The current page is copied into `page`, so a scan
 * keeps no PF page fixed between calls; changes made to the page currently
 * being scanned become visible from the next page onward. */
typedef struct SP_Scan {
    int spfd;
    int pageNum;               /* page currently copied, or next to load */
    int slot;                  /* next slot to examine                   */
    int loaded;                /* `page` holds pageNum                   */
    int open;
    char page[PF_PAGE_SIZE];
} SP_Scan;

int SP_CreateFile(char *fname);
int SP_DestroyFile(char *fname);
/* policy: PF_POLICY_LRU or PF_POLICY_MRU; search: REC_SEARCH_*.
 * Returns a record-file descriptor (>= 0) or a negative error. */
int SP_OpenFile(char *fname, int policy, int search);
int SP_CloseFile(int spfd);

/* Inserts len bytes (1..SP_MAX_RECORD) and returns the new RID in *rid. */
int SP_InsertRec(int spfd, const char *rec, int len, int *rid);
/* Deletes a record and compacts its page; other RIDs are unaffected. */
int SP_DeleteRec(int spfd, int rid);
/* Copies a record into buf.  *len receives its length; if bufSize is too
 * small, RECE_BUFSMALL is returned and *len still says how much is needed. */
int SP_GetRec(int spfd, int rid, char *buf, int bufSize, int *len);

/* Returns live records in (page, slot) order. */
int SP_ScanOpen(int spfd, SP_Scan *scan);
int SP_ScanNext(SP_Scan *scan, int *rid, char *buf, int bufSize, int *len);
int SP_ScanClose(SP_Scan *scan);

/* Reads every page and classifies all of its bytes (see REC_SpaceStats). */
int SP_GetSpaceStats(int spfd, REC_SpaceStats *stats);
int SP_GetPageInfo(int spfd, int pageNum, SP_PageInfo *info);
int SP_NumPages(int spfd);

#endif
