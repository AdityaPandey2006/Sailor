/* ixindex.h: B+ tree index on the Student file's roll-no (assignment part 3).
 *
 * Everything here is built from existing layers and changes none of them:
 *
 *   PF  (toydb/pflayer)  paged files, buffer pool, I/O statistics (part 1)
 *   SP  (toydb/splayer)  slotted-page Student file, RIDs (part 2)
 *   AM  (toydb/amlayer)  the supplied B+ tree: AM_CreateIndex,
 *                        AM_InsertEntry, AM_OpenIndexScan, ...
 *
 * Index format: the unmodified AM format.  An index is the PF file
 * "<studentFile>.<indexNo>" created by AM_CreateIndex; page 0 is always the
 * root.  See README.md ("Index file format") for the page layouts.
 *
 * Key:  roll-no, the 2nd ';'-separated field of a student.txt line, stored
 *       as an AM 'c' (char) attribute of IX_KEY_LEN bytes, '\0'-padded.
 *       AM compares such keys with strncmp(), i.e. plain byte order.
 * RID:  the slotted-page record id, page * 65536 + slot (REC_MakeRid), which
 *       is the int recId the AM layer stores.
 * Duplicates: handled by AM itself - a key appears once per leaf and owns a
 *       list of RIDs; an EQUAL scan returns all of them.
 * Missing/oversized roll-no: the record is still stored in the Student file
 *       but is not indexed (counted in recordsSkipped).
 *
 * Methods 1 and 2 are implemented in ixbuild.c; Method 3 (bulk loading)
 * is implemented in ixbulk.c. All three use the shared method table,
 * measurement, query and validation code below.
 */
#ifndef TOYDB_IXINDEX_H
#define TOYDB_IXINDEX_H

#include "pf.h"
#include "studentdata.h"

/* ------------------------------------------------------------------ keys */
#define IX_KEY_TYPE   'c'
#define IX_KEY_LEN    8      /* every roll-no in student.txt is 8 chars    */
#define IX_KEY_FIELD  1      /* 0-based ';' field holding the roll-no       */
#define IX_INDEX_NO   0      /* index number -> file "<studentFile>.0"      */

/* Error codes of this module (AM codes -1..-11, PF -1..-22 and record-layer
 * codes -101..-110 are passed through unchanged). */
#define IXE_OK        0
#define IXE_NOKEY     -201   /* record has no usable roll-no (not an error
                                for builders: the record is skipped)        */
#define IXE_INVALID   -202   /* validation found a wrong index              */
#define IXE_NOMEM     -203
#define IXE_IO        -204   /* query file / work file problem              */

/* Copies the roll-no of a raw student record into key[IX_KEY_LEN],
 * '\0'-padded.  Returns IXE_OK, or IXE_NOKEY if the field is missing,
 * empty or longer than IX_KEY_LEN. */
int IX_ExtractKey(const char *rec, int len, char key[IX_KEY_LEN]);

/* Formats a key for printing (stops at the first '\0'). */
const char *IX_KeyString(const char key[IX_KEY_LEN], char out[IX_KEY_LEN + 1]);

/* Name of the index file of studentFile: "<studentFile>.<indexNo>". */
void IX_IndexFileName(const char *studentFile, int indexNo, char *out, int outSize);

/* Removes the Student file and its index file if they exist. */
void IX_RemoveFiles(const char *studentFile, int indexNo);

const char *IX_ErrorString(int code);

/* ------------------------------------------------------------ measuring
 * A measurement is the difference between two snapshots of the wall clock
 * (CLOCK_MONOTONIC) and of the pool-wide PF counters.  Snapshots are taken
 * with PF_GetStats(), never PF_ResetStats(), so measurements can nest and
 * the counters are never disturbed.
 *
 * Counter meaning (see toydb/pflayer/README.md):
 *   logical_reads    page requests served by the buffer pool
 *                    (PF_GetThisPage/GetFirstPage/GetNextPage/AllocPage):
 *                    the "pages accessed" metric
 *   physical_reads   pages actually read from disk (buffer misses)
 *   physical_writes  pages actually written to disk (evictions and flushes)
 *   logical_writes   update notifications (dirty unfixes / PF_MarkDirty)
 */
typedef struct IX_Measure {
    double seconds;
    PF_Stats pf;
} IX_Measure;

typedef struct IX_Timer {
    double t0;
    PF_Stats s0;
} IX_Timer;

double IX_Now(void);                                   /* seconds, monotonic */
void IX_TimerStart(IX_Timer *t);
void IX_TimerStop(const IX_Timer *t, IX_Measure *m);   /* m = now - start    */
void IX_MeasureAdd(IX_Measure *sum, const IX_Measure *m);

/* ------------------------------------------------------------- building */
typedef struct IX_BuildSpec {
    const StudentData *data;   /* dataset, in student.txt order            */
    const char *studentFile;   /* Student file path (created by the method) */
    int indexNo;               /* index file = "<studentFile>.<indexNo>"   */
    int policy;                /* PF_POLICY_LRU or PF_POLICY_MRU            */
} IX_BuildSpec;

typedef struct IX_BuildResult {
    long recordsTotal;     /* records stored in the Student file          */
    long recordsIndexed;   /* indexed (key, RID) pairs, including bulk load */
    long recordsSkipped;   /* records without a usable roll-no             */
    IX_Measure setup;      /* work that is not index construction, e.g.
                              writing the Student file before Method 1    */
    IX_Measure build;      /* index construction (see each method)         */
} IX_BuildResult;

/* A complete index-construction method.  It must create spec->studentFile
 * and its index from spec->data, close both files, and fill *r.
 * Returns IXE_OK or a negative error. */
typedef int (*IX_BuildFn)(const IX_BuildSpec *spec, IX_BuildResult *r);

typedef struct IX_Method {
    int id;
    const char *name;
    const char *description;
    IX_BuildFn build;
} IX_Method;

extern const IX_Method IX_Methods[];   /* terminated by {0, NULL, ...} */
const IX_Method *IX_FindMethod(int id);

/* Optional hook called by IX_BuildIncremental every `every` inserts while
 * both files are open.  Used by the tests to check that the index is valid
 * throughout construction.  Must not leave pages fixed; return <0 to abort. */
typedef int (*IX_ProgressFn)(void *ctx, int spfd, int amfd, long inserted);

/* Writes every record of d into a new Student file (no index).  Used as
 * the setup step of Method 1. */
int IX_LoadStudentFile(const StudentData *d, const char *studentFile, int policy,
                       long *stored);

/* Method 1: index an existing Student file.  Opens it, creates the index,
 * scans the file sequentially and inserts (roll-no, RID) for every record.
 * r->build covers create + scan + inserts + closing both files; the
 * one-time cost of opening the Student file is reported in *openCost. */
int IX_BuildFromExisting(const char *studentFile, int indexNo, int policy,
                         IX_BuildResult *r, IX_Measure *openCost);

/* Method 2: start from an empty Student file and empty index; insert each
 * record into the file, then its (roll-no, RID) into the index.
 * r->build covers everything (both files are built together). */
int IX_BuildIncremental(const IX_BuildSpec *spec, IX_BuildResult *r,
                        IX_ProgressFn progress, void *ctx, long every);

/* IX_BuildFn wrappers registered in IX_Methods[]. */
int IX_Method1(const IX_BuildSpec *spec, IX_BuildResult *r);
int IX_Method2(const IX_BuildSpec *spec, IX_BuildResult *r);
int IX_Method3(const IX_BuildSpec *spec, IX_BuildResult *r);

/* AM_OpenIndexScan on the roll-no index (key type and length filled in).
 * Returns a scan descriptor or a negative AM error.  Close it with
 * AM_CloseIndexScan.  Scans rely on two fixes made to amlayer/amscan.c
 * (see README.md, "Changes to the AM layer"). */
int IX_OpenScan(int amfd, int op, char *key);

/* -------------------------------------------------------------- queries */
typedef struct IX_QuerySet {
    int n;
    char (*key)[IX_KEY_LEN];
} IX_QuerySet;

/* Deterministic query set: `hits` roll-nos drawn (with a seeded LCG) from
 * the dataset's indexed keys, then `misses` keys known to be absent,
 * shuffled together.  Same dataset + seed -> same queries in the same order. */
int IX_MakeQueries(const StudentData *d, int hits, int misses, unsigned seed,
                   IX_QuerySet *q);
/* Query files hold one key per line (so every method can reuse them). */
int IX_SaveQueries(const char *path, const IX_QuerySet *q);
int IX_LoadQueries(const char *path, IX_QuerySet *q);
void IX_FreeQueries(IX_QuerySet *q);

typedef struct IX_QueryResult {
    long queries;
    long found;              /* queries that returned >= 1 RID           */
    long notFound;
    long rids;               /* RIDs returned in total                   */
    unsigned long long ridSum;  /* order-sensitive checksum of the answers */
    IX_Measure m;
} IX_QueryResult;

/* Runs every query as an AM EQUAL index scan on an already-open index
 * (amfd).  Only the scans are measured. */
int IX_RunQueriesFd(int amfd, const IX_QuerySet *q, IX_QueryResult *r);

/* The common benchmark step: opens the index file (its pages are not in the
 * buffer pool, because PF_CloseFile releases them), measures the query
 * set, and closes the file again.  File open/close are not measured. */
int IX_RunQueries(const char *studentFile, int indexNo, const IX_QuerySet *q,
                  IX_QueryResult *r);

/* ----------------------------------------------------- validation/shape */
typedef struct IX_TreeInfo {
    int height;               /* 1 = root is a leaf                        */
    long internalPages, leafPages;
    long keys;                /* distinct keys in the leaves               */
    long entries;             /* (key, RID) pairs                          */
    int rootMaxKeys;          /* maxKeys stored in the root header         */
    double leafFill;          /* used bytes / PF_PAGE_SIZE, mean over leaves */
} IX_TreeInfo;

/* Walks the whole tree read-only and checks the B+ tree invariants:
 * page types, attrLength, sorted keys inside every node, separator ranges,
 * equal leaf depth, non-empty RID lists, and that the leaf chain visits the
 * leaves left to right.  Returns IXE_OK or IXE_INVALID (reason printed). */
int IX_InspectTreeFd(int amfd, IX_TreeInfo *t);
int IX_InspectTree(const char *studentFile, int indexNo, IX_TreeInfo *t);

typedef struct IX_ValidateResult {
    long expectedEntries;     /* dataset records with a usable roll-no     */
    long scannedEntries;      /* entries returned by a full (ALL) scan      */
    long distinctKeys;
    long keysChecked;         /* EQUAL scans, one per distinct key          */
    long absentChecked;       /* EQUAL scans for keys not in the dataset    */
    long duplicateKeys;       /* keys with more than one RID                */
} IX_ValidateResult;

/* Opens the Student file and its index and checks, against the dataset:
 *  - a full index scan returns exactly expectedEntries RIDs in key order;
 *  - for every distinct roll-no, an EQUAL scan returns exactly as many RIDs
 *    as the dataset has records with that roll-no, all distinct, each
 *    pointing to a live Student record whose roll-no is that key;
 *  - EQUAL scans for absent keys return nothing.
 * Together these prove the index maps every record to its own RID.
 * Closes both files.  Returns IXE_OK or a negative error. */
int IX_Validate(const char *studentFile, int indexNo, const StudentData *d,
                IX_ValidateResult *v);

#endif
