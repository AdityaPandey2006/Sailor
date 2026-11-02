/* testutil.h: small helpers shared by the C test programs.
 *
 * Tests compare a record file against a "shadow" - an in-memory list of the
 * (rid, bytes) pairs that should be live - and stop at the first mismatch.
 * Every helper is static so each test program stays a single translation unit. */
#ifndef TOYDB_TESTUTIL_H
#define TOYDB_TESTUTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recfile.h"

/* CHECK(cond): print the failing expression and exit with status 1. */
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    exit(1); } } while (0)
/* CHECK_EQ for ints, printing both values. */
#define CHECK_EQ(a, b) do { long long _a = (a), _b = (b); if (_a != _b) { \
    fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %lld, %s == %lld\n", \
            __FILE__, __LINE__, #a, _a, #b, _b); exit(1); } } while (0)

static unsigned long test_rng = 12345;
static unsigned test_rand(void)            /* deterministic 32-bit LCG */
{
    test_rng = (1664525UL * test_rng + 1013904223UL) & 0xffffffffUL;
    return (unsigned)(test_rng >> 8);
}

/* Fills buf with len bytes that encode `tag`, so records are distinguishable.
 * Bytes are printable and never '\0' (the fixed layout's pad byte). */
static void make_record(char *buf, int len, int tag)
{
    int i;
    for (i = 0; i < len; i++)
        buf[i] = (char)('A' + (tag * 7 + i) % 26);
    if (len >= 4) {
        buf[0] = (char)('0' + tag % 10);
        buf[1] = (char)('0' + tag / 10 % 10);
        buf[2] = (char)('0' + tag / 100 % 10);
        buf[3] = (char)('0' + tag / 1000 % 10);
    }
}

typedef struct Shadow {
    int n, cap;
    int *rid, *len;
    char **data;
} Shadow;

static void shadow_add(Shadow *s, int rid, const char *data, int len)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 256;
        s->rid = realloc(s->rid, s->cap * sizeof *s->rid);
        s->len = realloc(s->len, s->cap * sizeof *s->len);
        s->data = realloc(s->data, s->cap * sizeof *s->data);
        CHECK(s->rid && s->len && s->data);
    }
    s->rid[s->n] = rid;
    s->len[s->n] = len;
    CHECK((s->data[s->n] = malloc(len > 0 ? len : 1)) != NULL);
    memcpy(s->data[s->n], data, len);
    s->n++;
}

static int shadow_find(const Shadow *s, int rid)
{
    int i;
    for (i = 0; i < s->n; i++)
        if (s->rid[i] == rid)
            return i;
    return -1;
}

static void shadow_remove(Shadow *s, int i)
{
    free(s->data[i]);
    s->n--;
    s->rid[i] = s->rid[s->n];
    s->len[i] = s->len[s->n];
    s->data[i] = s->data[s->n];
}

static void shadow_clear(Shadow *s)
{
    while (s->n > 0)
        shadow_remove(s, s->n - 1);
}

static void shadow_free(Shadow *s)
{
    shadow_clear(s);
    free(s->rid);
    free(s->len);
    free(s->data);
    memset(s, 0, sizeof *s);
}

static long shadow_bytes(const Shadow *s)
{
    long b = 0;
    int i;
    for (i = 0; i < s->n; i++)
        b += s->len[i];
    return b;
}

/* `scanned` must list exactly the records of `expected`, in ascending RID
 * order (the order both layers' scans guarantee), with identical bytes. */
static void shadow_check_scan(const Shadow *expected, const Shadow *scanned)
{
    int i;
    CHECK_EQ(scanned->n, expected->n);
    for (i = 0; i < scanned->n; i++) {
        int j = shadow_find(expected, scanned->rid[i]);
        if (i > 0)
            CHECK(scanned->rid[i] > scanned->rid[i - 1]);
        CHECK(j >= 0);
        CHECK_EQ(scanned->len[i], expected->len[j]);
        CHECK(memcmp(scanned->data[i], expected->data[j], scanned->len[i]) == 0);
    }
}

/* Every byte of every page must be accounted for exactly once. */
static void check_space_invariant(const REC_SpaceStats *st, const Shadow *s)
{
    CHECK_EQ(st->recordBytes + st->metadataBytes + st->paddingBytes + st->freeBytes,
             st->numPages * (long)PF_PAGE_SIZE);
    CHECK_EQ(st->numRecords, s->n);
    CHECK_EQ(st->recordBytes, shadow_bytes(s));
}

#endif
