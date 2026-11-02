/* recfile.c: RID encoding, error strings and utilization shared by both
 * record layers.  See recfile.h. */
#include "recfile.h"

int REC_MakeRid(int page, int slot)
{
    if (page < 0 || page > REC_MAX_PAGE || slot < 0 || slot > REC_MAX_SLOT)
        return RECE_INVALIDRID;
    /* page <= 32767 keeps the result within a non-negative signed int */
    return page * 65536 + slot;
}

int REC_SplitRid(int rid, int *page, int *slot)
{
    if (rid < 0)
        return RECE_INVALIDRID;
    *page = rid / 65536;
    *slot = rid % 65536;
    return RECE_OK;
}

const char *REC_ErrorString(int code)
{
    switch (code) {
    case RECE_OK:         return "ok";
    case RECE_INVALIDRID: return "invalid record id";
    case RECE_RECSIZE:    return "record empty or too long";
    case RECE_EOF:        return "end of scan";
    case RECE_FD:         return "invalid record-file descriptor";
    case RECE_NOMEM:      return "out of memory";
    case RECE_FTABFULL:   return "record-file table full";
    case RECE_BADPAGE:    return "page is not a valid page of this layout";
    case RECE_BUFSMALL:   return "buffer too small";
    case RECE_INVALIDARG: return "invalid argument";
    case RECE_FILEFULL:   return "record file full";
    default:              return code < 0 ? "PF layer error" : "unknown";
    }
}

double REC_Utilization(const REC_SpaceStats *s)
{
    if (s->numPages <= 0)
        return 0.0;
    return (double)s->recordBytes / ((double)s->numPages * PF_PAGE_SIZE);
}
