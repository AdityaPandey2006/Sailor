/* studentdata.h: reads data/student.txt into memory as raw records.
 *
 * Decision 1: each line is one record, stored byte-for-byte (semicolons
 * included).  The first `skipLines` lines (the "Database dummy - table
 * student" title) and empty lines are skipped; the line terminator
 * ("\n" or "\r\n") is not part of the record. */
#ifndef TOYDB_STUDENTDATA_H
#define TOYDB_STUDENTDATA_H

typedef struct StudentData {
    int count;
    int maxLen;
    long totalBytes;
    char **rec;     /* rec[i] is NOT '\0'-terminated; use len[i] */
    int *len;
} StudentData;

/* Returns 0 on success, -1 on I/O or memory error (message printed). */
int SD_Load(const char *path, int skipLines, StudentData *out);
void SD_Free(StudentData *d);

/* Order-independent fingerprint of a multiset of records: the 64-bit sum
 * of FNV-1a hashes.  Equal fingerprints (with equal counts and byte totals)
 * mean a scan returned the same records that were inserted. */
unsigned long long SD_Hash(const char *rec, int len);

#endif
