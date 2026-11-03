/* Bottom-up bulk loading in the existing AM on-disk format.
 * Sorting and sorted Student-file creation are setup; construction consumes
 * the resulting in-memory (key,RID) stream. No AM insertion/search replacement.
 */
#include <stdlib.h>
#include <string.h>
#include "amwrap.h"
#include "slotted.h"
#include "ixindex.h"

typedef struct Entry {
    char key[IX_KEY_LEN];
    int original, rid, usable;
} Entry;
typedef struct Group { int first, count, bytes; } Group;
typedef struct Node { int page; char first[IX_KEY_LEN]; } Node;

static int compare(const void *a, const void *b)
{
    const Entry *x=a, *y=b;
    int c;
    if (x->usable != y->usable) return y->usable-x->usable;
    c=memcmp(x->key,y->key,IX_KEY_LEN);
    return c ? c : (x->original>y->original)-(x->original<y->original);
}
static int maxkeys(void)
{
    int n=(PF_PAGE_SIZE-AM_sint-AM_si)/(IX_KEY_LEN+AM_si);
    return n-n%2;
}
static int allocate(int fd, int *page)
{
    char *buf;
    int e=PF_AllocPage(fd,page,&buf);
    if(e<0) return e;
    memset(buf,0,PF_PAGE_SIZE);
    return PF_UnfixPage(fd,*page,TRUE);
}
static int leaf(int fd, int page, int next, const Entry *e,
                const Group *g, int first, int end)
{
    char *buf; int err, i, j;
    AM_LEAFHEADER h;
    if((err=PF_GetThisPage(fd,page,&buf))<0) return err;
    memset(buf,0,PF_PAGE_SIZE); memset(&h,0,sizeof h);
    h.pageType='l'; h.nextLeafPage=next; h.attrLength=IX_KEY_LEN;
    h.maxKeys=maxkeys(); h.numKeys=end-first;
    h.keyPtr=AM_sl+h.numKeys*(IX_KEY_LEN+AM_ss); h.recIdPtr=PF_PAGE_SIZE;
    for(i=first;i<end;i++) {
        short head=0;
        int off=AM_sl+(i-first)*(IX_KEY_LEN+AM_ss);
        memcpy(buf+off,e[g[i].first].key,IX_KEY_LEN);
        /* All duplicates belong to this one key and this one leaf. */
        for(j=g[i].count-1;j>=0;j--) {
            h.recIdPtr-=AM_si+AM_ss;
            memcpy(buf+h.recIdPtr,&e[g[i].first+j].rid,AM_si);
            memcpy(buf+h.recIdPtr+AM_si,&head,AM_ss);
            head=h.recIdPtr;
        }
        memcpy(buf+off+IX_KEY_LEN,&head,AM_ss);
    }
    memcpy(buf,&h,AM_sl);
    return PF_UnfixPage(fd,page,TRUE);
}
static int internal(int fd, int page, const Node *children, int count)
{
    char *buf; int i, err; AM_INTHEADER h;
    if((err=PF_GetThisPage(fd,page,&buf))<0) return err;
    memset(buf,0,PF_PAGE_SIZE); memset(&h,0,sizeof h);
    h.pageType='i'; h.attrLength=IX_KEY_LEN;
    h.maxKeys=maxkeys(); h.numKeys=count-1;
    memcpy(buf,&h,AM_sint);
    for(i=0;i<count;i++) {
        int off=AM_sint+i*(IX_KEY_LEN+AM_si);
        memcpy(buf+off,&children[i].page,AM_si);
        if(i) memcpy(buf+off-IX_KEY_LEN,children[i].first,IX_KEY_LEN);
    }
    return PF_UnfixPage(fd,page,TRUE);
}

static int build(const IX_BuildSpec *spec, Entry *e, int n)
{
    Group *g=NULL; Node *nodes=NULL, *parents=NULL;
    int *starts=NULL; int ng=0, nl=0, i, j, used, fd=-1, err=IXE_OK, root;
    char name[1100];
    /* At most n groups and n leaf boundaries; allocate even for empty input. */
    g=calloc((size_t)n+1,sizeof *g);
    starts=calloc((size_t)n+2,sizeof *starts);
    if(!g || !starts) { err=IXE_NOMEM; goto done; }
    for(i=0;i<n;i=j) {
        for(j=i+1;j<n && !memcmp(e[i].key,e[j].key,IX_KEY_LEN);j++);
        g[ng].first=i; g[ng].count=j-i;
        g[ng].bytes=IX_KEY_LEN+AM_ss+(j-i)*(AM_si+AM_ss);
        if(g[ng].bytes>PF_PAGE_SIZE-AM_sl) { err=IXE_INVALID; goto done; }
        ng++;
    }
    /* Greedy byte packing, followed by balancing the last two leaves.
     * Capacity is measured in bytes because duplicates have variable cost. */
    for(i=0;i<ng;) {
        starts[nl++]=i; used=AM_sl;
        while(i<ng && used+g[i].bytes<=PF_PAGE_SIZE) used+=g[i++].bytes;
    }
    if(!nl) nl=1;
    starts[nl]=ng;
    if(nl>1) {
        int a=starts[nl-2], b=starts[nl-1], end=ng;
        int left=0,right=0;
        for(i=a;i<b;i++) left+=g[i].bytes;
        for(i=b;i<end;i++) right+=g[i].bytes;
        while(b>a+1 && right+g[b-1].bytes<=PF_PAGE_SIZE-AM_sl &&
              abs((left-g[b-1].bytes)-(right+g[b-1].bytes))<abs(left-right)) {
            left-=g[b-1].bytes; right+=g[b-1].bytes; b--;
        }
        starts[nl-1]=b;
    }
    IX_IndexFileName(spec->studentFile,spec->indexNo,name,sizeof name);
    if((err=PF_CreateFile(name))<0) goto done;
    fd=PF_OpenFileWithPolicy(name,spec->policy);
    if(fd<0) { err=fd; goto done; }
    AM_RootPageNum=0;
    if((err=allocate(fd,&root))<0) goto done;
    if(root!=0) { err=IXE_INVALID; goto done; }
    if(nl==1) { err=leaf(fd,0,AM_NULL_PAGE,e,g,0,ng); goto done; }
    nodes=calloc(nl,sizeof *nodes);
    if(!nodes) { err=IXE_NOMEM; goto done; }
    /* Allocate page 1 for the LAST leaf, then page 2 for the FIRST leaf.
     * Every allocated page is reachable, and AM's page-2 convention holds. */
    if((err=allocate(fd,&nodes[nl-1].page))<0) goto done;
    for(i=0;i<nl-1;i++) if((err=allocate(fd,&nodes[i].page))<0) goto done;
    for(i=0;i<nl;i++) {
        memcpy(nodes[i].first,e[g[starts[i]].first].key,IX_KEY_LEN);
        err=leaf(fd,nodes[i].page,i+1<nl?nodes[i+1].page:AM_NULL_PAGE,
                 e,g,starts[i],starts[i+1]);
        if(err<0) goto done;
    }
    /* Balanced fanout avoids a singleton or underfilled final internal node. */
    while(nl>maxkeys()+1) {
        int np=(nl+maxkeys())/(maxkeys()+1), base=nl/np, extra=nl%np, at=0;
        parents=calloc(np,sizeof *parents);
        if(!parents) { err=IXE_NOMEM; goto done; }
        for(i=0;i<np;i++) {
            int count=base+(i<extra);
            if((err=allocate(fd,&parents[i].page))<0) goto done;
            memcpy(parents[i].first,nodes[at].first,IX_KEY_LEN);
            if((err=internal(fd,parents[i].page,nodes+at,count))<0) goto done;
            at+=count;
        }
        free(nodes); nodes=parents; parents=NULL; nl=np;
    }
    err=internal(fd,0,nodes,nl);
done:
    if(fd>=0) { int close=PF_CloseFile(fd); if(err==IXE_OK) err=close; }
    free(g); free(starts); free(nodes); free(parents);
    return err;
}

int IX_Method3(const IX_BuildSpec *spec, IX_BuildResult *r)
{
    const StudentData *d=spec->data;
    Entry *entries; IX_Timer timer;
    int i, fd=-1, err=IXE_OK, n=0;
    memset(r,0,sizeof *r);
    IX_TimerStart(&timer);
    entries=calloc((size_t)d->count+1,sizeof *entries);
    if(!entries) return IXE_NOMEM;
    for(i=0;i<d->count;i++) {
        entries[i].original=i;
        entries[i].usable=IX_ExtractKey(d->rec[i],d->len[i],entries[i].key)==IXE_OK;
        n+=entries[i].usable;
    }
    qsort(entries,d->count,sizeof *entries,compare);
    if((err=SP_CreateFile((char *)spec->studentFile))<0) goto setup_done;
    fd=SP_OpenFile((char *)spec->studentFile,spec->policy,REC_SEARCH_CACHED);
    if(fd<0) { err=fd; goto setup_done; }
    for(i=0;i<d->count;i++) {
        int orig=entries[i].original;
        if((err=SP_InsertRec(fd,d->rec[orig],d->len[orig],&entries[i].rid))<0) break;
        r->recordsTotal++;
    }
setup_done:
    if(fd>=0) { int close=SP_CloseFile(fd); if(err==IXE_OK) err=close; }
    IX_TimerStop(&timer,&r->setup);
    if(err==IXE_OK) {
        IX_TimerStart(&timer);
        err=build(spec,entries,n);
        IX_TimerStop(&timer,&r->build);
        if(err==IXE_OK) { r->recordsIndexed=n; r->recordsSkipped=d->count-n; }
    }
    free(entries);
    return err;
}
