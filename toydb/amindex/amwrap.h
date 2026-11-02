/* amwrap.h: includes the supplied AM header from modern C code.
 *
 * The supplied am.h (toydb/amlayer/am.h) is pre-ANSI: it declares
 * `extern char *calloc(); extern char *malloc();`, which conflicts with
 * <stdlib.h>, and it has no prototypes and no include guard.  This wrapper
 * hides the two conflicting declarations, includes am.h exactly once, and
 * adds prototypes for the AM entry points used by the index code.
 *
 * The prototypes use `int` where the K&R definitions declare `char`
 * parameters, because a K&R `char` parameter is received as a promoted int;
 * this is the prototype that is compatible with those definitions.
 *
 * No AM source file is changed.
 */
#ifndef TOYDB_AMWRAP_H
#define TOYDB_AMWRAP_H

#include <stdlib.h>
#include "pf.h"

#define calloc amwrap_hidden_calloc
#define malloc amwrap_hidden_malloc
#include "am.h"
#undef calloc
#undef malloc

#if PF_PAGE_SIZE != 4096
#error "amindex must be built against toydb/pflayer/pf.h (PF_PAGE_SIZE 4096)"
#endif

/* PF call used to open an index file (pf.h has no prototype for it). */
int PF_OpenFile(char *fname);

/* AM interface (definitions in toydb/amlayer/amfns.c and amscan.c). */
int AM_CreateIndex(char *fileName, int indexNo, int attrType, int attrLength);
int AM_DestroyIndex(char *fileName, int indexNo);
int AM_InsertEntry(int fileDesc, int attrType, int attrLength, char *value, int recId);
int AM_DeleteEntry(int fileDesc, int attrType, int attrLength, char *value, int recId);
int AM_OpenIndexScan(int fileDesc, int attrType, int attrLength, int op, char *value);
int AM_FindNextEntry(int scanDesc);
int AM_CloseIndexScan(int scanDesc);
int AM_PrintError(char *s);

#endif
