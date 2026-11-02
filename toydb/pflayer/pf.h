#ifndef TOYDB_PF_H
#define TOYDB_PF_H
/* pf.h: externs and error codes for Paged File Interface*/
#ifndef TRUE
#define TRUE 1		
#endif
#ifndef FALSE
#define FALSE 0
#endif

/************** Error Codes *********************************/
#define PFE_OK		0	/* OK */
#define PFE_NOMEM	-1	/* no memory */
#define PFE_NOBUF	-2	/* no buffer space */
#define PFE_PAGEFIXED 	-3	/* page already fixed in buffer */
#define PFE_PAGENOTINBUF -4	/* page to be unfixed is not in the buffer */
#define PFE_UNIX	-5	/* unix error */
#define PFE_INCOMPLETEREAD -6	/* incomplete read of page from file */
#define PFE_INCOMPLETEWRITE -7	/* incomplete write of page to file */
#define PFE_HDRREAD	-8	/* incomplete read of header from file */
#define PFE_HDRWRITE	-9	/* incomplte write of header to file */
#define PFE_INVALIDPAGE -10	/* invalid page number */
#define PFE_FILEOPEN	-11	/* file already open */
#define	PFE_FTABFULL	-12	/* file table is full */
#define PFE_FD		-13	/* invalid file descriptor */
#define PFE_EOF		-14	/* end of file */
#define PFE_PAGEFREE	-15	/* page already free */
#define PFE_PAGEUNFIXED	-16	/* page already unfixed */

/* Internal error: please report to the TA */
#define PFE_PAGEINBUF	-17	/* new page to be allocated already in buffer */
#define PFE_HASHNOTFOUND -18	/* hash table entry not found */
#define PFE_HASHPAGEEXIST -19	/* page already exist in hash table */

//error codes added for variable buffer size that
//we are taking as input from user as a parameter are defined below(reqd by part 1 of assignment)
#define PFE_INVALIDBUFSIZE -20 /* buffer count must be positive */
#define PFE_BUFINIT -21 /* buffer size must be set before pages are allocated */


/* page size */
#define PF_PAGE_SIZE	4096

/* externs from the PF layer */
extern int PFerrno;		/* error number of last error */
extern void PF_Init();
/* Call once at startup, before opening files or allocating pages.
 * PF_Init() remains available with the default capacity of 20 pages. */
extern int PF_InitWithBufferSize(int buffer_count);
extern void PF_PrintError();

//added these 2 codes: PF_POLICY_LRU,PF_POLICY_MRU for helping us to choose between LRU and MRU
//also added 
#define PFE_INVALIDPOLICY -22
#define PF_POLICY_LRU 0
#define PF_POLICY_MRU 1
extern int PF_OpenFileWithPolicy(char *fname, int policy);
extern int PF_GetFilePolicy(int fd);
/* Mark a fixed page modified without unfixing or immediately writing it.
 * Also marks it most recently used. Unfix(FALSE) preserves its dirty flag. */
extern int PF_MarkDirty(int fd, int pagenum);

/* Pool-wide counters since the last reset. Page transfers exclude file headers. */
typedef struct PF_Stats {
    unsigned long long logical_reads, logical_writes;
    unsigned long long physical_reads, physical_writes;
    unsigned long long buffer_hits, buffer_misses;
} PF_Stats;
extern void PF_ResetStats(void);
extern PF_Stats PF_GetStats(void);
extern void PF_PrintStats(void);
#endif
