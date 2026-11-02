/* recfile.h: definitions shared by the two record-file layers built on PF.
 *
 *   slotted.h  - variable-length records in slotted pages (assignment part 2)
 *   fixedrec.h - fixed-length ("static") records, the comparison baseline
 *
 * Both layers are separate implementations; they only share the record
 * identifier (RID) encoding, error codes, search-mode constants and the
 * space-accounting structure below, so their results can be compared on
 * identical terms.
 */
#ifndef TOYDB_RECFILE_H
#define TOYDB_RECFILE_H

#include "pf.h"

/* Full prototypes for the PF calls used here.  The supplied pf.h does not
 * declare them; the K&R definitions in pf.c take exactly these types. */
extern int PF_CreateFile(char *fname);
extern int PF_DestroyFile(char *fname);
extern int PF_CloseFile(int fd);
extern int PF_GetFirstPage(int fd, int *pagenum, char **pagebuf);
extern int PF_GetNextPage(int fd, int *pagenum, char **pagebuf);
extern int PF_GetThisPage(int fd, int pagenum, char **pagebuf);
extern int PF_AllocPage(int fd, int *pagenum, char **pagebuf);
extern int PF_UnfixPage(int fd, int pagenum, int dirty);

/* ---------------------------------------------------------------------
 * Record identifiers
 *
 * A RID names one record by (page number, slot number).  The AM layer
 * stores record ids as a single C int (AM_InsertEntry(..., int recId)),
 * so the pair is packed as
 *
 *     RID = page * 65536 + slot        (page in [0, 32767], slot in [0, 65535])
 *
 * The largest RID is 32767 * 65536 + 65535 = 2^31 - 1 = INT_MAX, so every
 * valid RID is a non-negative int and negative values stay free for errors.
 * --------------------------------------------------------------------- */
#define REC_MAX_PAGE  32767
#define REC_MAX_SLOT  65535
#define REC_MAX_PAGES (REC_MAX_PAGE + 1)   /* pages a record file may hold */

/* Returns the packed RID, or RECE_INVALIDRID if page/slot is out of range. */
int REC_MakeRid(int page, int slot);
/* Splits a RID; returns RECE_OK or RECE_INVALIDRID for a negative RID. */
int REC_SplitRid(int rid, int *page, int *slot);

/* ---------------------------------------------------------------------
 * Error codes.  PF errors (-1 .. -22) are passed through unchanged, so the
 * record-layer codes start at -100 to avoid overlapping them.
 * --------------------------------------------------------------------- */
#define RECE_OK          0
#define RECE_INVALIDRID  -101  /* RID out of range, or no live record there */
#define RECE_RECSIZE     -102  /* record empty or too long for this layout  */
#define RECE_EOF         -103  /* scan finished                              */
#define RECE_FD          -104  /* invalid record-file descriptor             */
#define RECE_NOMEM       -105  /* malloc failed                              */
#define RECE_FTABFULL    -106  /* record-file table full                     */
#define RECE_BADPAGE     -107  /* page bytes are not a valid page of this layout */
#define RECE_BUFSMALL    -108  /* caller's buffer too small for the record   */
#define RECE_INVALIDARG  -109  /* other invalid argument                     */
#define RECE_FILEFULL    -110  /* file reached REC_MAX_PAGES pages            */

/* Returns a short message for a record-layer or PF error code. */
const char *REC_ErrorString(int code);

/* ---------------------------------------------------------------------
 * First-fit search modes (design decision 5).  Both modes place every
 * record on exactly the same page; they differ only in how that page is
 * found, and therefore in PF I/O:
 *
 *   REC_SEARCH_CACHED  keep each page's free space in memory (rebuilt by
 *                      one scan when the file is opened) and fetch only the
 *                      chosen page.
 *   REC_SEARCH_SCAN    fetch pages 0, 1, 2, ... through the buffer pool
 *                      until one fits - the literal textbook first fit.
 * --------------------------------------------------------------------- */
#define REC_SEARCH_CACHED 0
#define REC_SEARCH_SCAN   1

/* ---------------------------------------------------------------------
 * Space accounting (design decision 4).  Every byte of every page falls
 * into exactly one bucket, so
 *
 *   recordBytes + metadataBytes + paddingBytes + freeBytes
 *       == numPages * PF_PAGE_SIZE
 *
 *   utilization = recordBytes / (numPages * PF_PAGE_SIZE)
 * --------------------------------------------------------------------- */
typedef struct REC_SpaceStats {
    long numPages;       /* data pages in the file                         */
    long numRecords;     /* live records                                   */
    long recordBytes;    /* bytes of actual record data                    */
    long metadataBytes;  /* page headers + slot directory / bitmap         */
    long paddingBytes;   /* fixed layout only: unused tail of occupied slots,
                            plus a page tail too small to hold another slot */
    long freeBytes;      /* space available to future inserts: free space
                            (slotted) or empty slots (fixed)                */
} REC_SpaceStats;

/* recordBytes / (numPages * PF_PAGE_SIZE), or 0 for an empty file. */
double REC_Utilization(const REC_SpaceStats *s);

#endif
