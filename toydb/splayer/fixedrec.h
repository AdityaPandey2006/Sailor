/* fixedrec.h: fixed-length ("static") record files on top of PF - the
 * baseline that slotted pages are compared against (design decision 3).
 *
 * Every record occupies exactly L bytes (the file's maximum record length).
 * Shorter records are padded with '\0' bytes; a record longer than L is
 * rejected with RECE_RECSIZE, never truncated.  Because padding marks the
 * end of a record, records must not contain '\0' bytes (text lines never do).
 *
 * Page layout (PF data area, 4096 bytes, 16-bit fields, no pointers):
 *
 *   offset 0  magic      FX_MAGIC
 *   offset 2  recLen     L
 *   offset 4  capacity   n = slots per page
 *   offset 6  liveCount  occupied slots
 *   offset 8  bitmap     ceil(n / 8) bytes, bit i set = slot i occupied
 *   then      n slots of L bytes each; any leftover tail is unusable
 *
 * n is the largest value with 8 + ceil(n/8) + n*L <= 4096.
 * Inserts use the same first-fit rule as the slotted layer (decision 5):
 * the lowest page with an empty slot, and its lowest empty slot.
 * A delete just clears the slot's bit; fixed slots need no compaction.
 */
#ifndef TOYDB_FIXEDREC_H
#define TOYDB_FIXEDREC_H

#include "recfile.h"

#define FX_MAGIC        0x4658                 /* "FX" */
#define FX_HEADER_SIZE  8
#define FX_MAX_RECLEN   (PF_PAGE_SIZE - FX_HEADER_SIZE - 1)   /* 4087: one slot + 1-byte bitmap */
#define FX_MAX_FILES    20

typedef struct FX_Scan {
    int fxfd;
    int pageNum, slot, loaded, open;
    char page[PF_PAGE_SIZE];   /* copy of the current page; nothing stays fixed */
} FX_Scan;

/* Slots per page for record length L, or 0 if L is out of range. */
int FX_SlotsPerPage(int recLen);

int FX_CreateFile(char *fname);
int FX_DestroyFile(char *fname);
/* recLen must match the pages already in the file (RECE_BADPAGE otherwise). */
int FX_OpenFile(char *fname, int policy, int recLen, int search);
int FX_CloseFile(int fxfd);

int FX_InsertRec(int fxfd, const char *rec, int len, int *rid);
int FX_DeleteRec(int fxfd, int rid);
int FX_GetRec(int fxfd, int rid, char *buf, int bufSize, int *len);

int FX_ScanOpen(int fxfd, FX_Scan *scan);
int FX_ScanNext(FX_Scan *scan, int *rid, char *buf, int bufSize, int *len);
int FX_ScanClose(FX_Scan *scan);

int FX_GetSpaceStats(int fxfd, REC_SpaceStats *stats);
int FX_NumPages(int fxfd);

#endif
