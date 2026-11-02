/* studentdata.c: see studentdata.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "studentdata.h"

int SD_Load(const char *path, int skipLines, StudentData *d)
{
    FILE *fp = fopen(path, "rb");
    char line[8192];
    int lineNo = 0, cap = 0;

    memset(d, 0, sizeof *d);
    if (!fp) {
        perror(path);
        return -1;
    }
    while (fgets(line, sizeof line, fp)) {
        int n = (int)strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') {
            fprintf(stderr, "%s: line %d too long\n", path, lineNo + 1);
            fclose(fp);
            SD_Free(d);
            return -1;
        }
        if (lineNo++ < skipLines)
            continue;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            n--;
        if (n == 0)
            continue;
        if (d->count == cap) {
            int newCap = cap ? cap * 2 : 1024;
            char **r = realloc(d->rec, newCap * sizeof *r);
            int *l = r ? realloc(d->len, newCap * sizeof *l) : NULL;
            if (r) d->rec = r;
            if (!r || !l) {
                fclose(fp);
                SD_Free(d);
                return -1;
            }
            d->len = l;
            cap = newCap;
        }
        if (!(d->rec[d->count] = malloc(n))) {
            fclose(fp);
            SD_Free(d);
            return -1;
        }
        memcpy(d->rec[d->count], line, n);
        d->len[d->count] = n;
        d->totalBytes += n;
        if (n > d->maxLen)
            d->maxLen = n;
        d->count++;
    }
    fclose(fp);
    return 0;
}

void SD_Free(StudentData *d)
{
    int i;
    for (i = 0; i < d->count; i++)
        free(d->rec[i]);
    free(d->rec);
    free(d->len);
    memset(d, 0, sizeof *d);
}

unsigned long long SD_Hash(const char *rec, int len)
{
    unsigned long long h = 1469598103934665603ULL;
    int i;
    for (i = 0; i < len; i++) {
        h ^= (unsigned char)rec[i];
        h *= 1099511628211ULL;
    }
    return h;
}
