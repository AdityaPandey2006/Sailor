/* Edge cases, byte preservation, and AM mutation compatibility. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amwrap.h"
#include "slotted.h"
#include "ixindex.h"
#include "testutil.h"

static void dataset(StudentData *d,int n,int duplicate)
{
    int i; memset(d,0,sizeof *d); d->count=n;
    d->rec=calloc(n+1,sizeof *d->rec); d->len=calloc(n+1,sizeof *d->len);
    CHECK(d->rec && d->len);
    for(i=0;i<n;i++) {
        char line[100]; int key=duplicate?0:n-i-1;
        snprintf(line,sizeof line,"%08d;K%07d;unchanged field %d;",i,key,i);
        d->len[i]=strlen(line); d->rec[i]=malloc(d->len[i]); CHECK(d->rec[i]);
        memcpy(d->rec[i],line,d->len[i]);
    }
}
static void preservation(const char *name,const StudentData *d)
{
    int fd=SP_OpenFile((char *)name,PF_POLICY_LRU,REC_SEARCH_CACHED);
    SP_Scan scan; int rid,len,err,i,count=0; char rec[SP_MAX_RECORD+1];
    unsigned char *seen=calloc(d->count+1,1); CHECK(seen && fd>=0);
    CHECK_EQ(SP_ScanOpen(fd,&scan),RECE_OK);
    while((err=SP_ScanNext(&scan,&rid,rec,sizeof rec,&len))==RECE_OK) {
        /* Every generated record contains its original ordinal in field 1. */
        char id[9]; CHECK(len>=9); memcpy(id,rec,8); id[8]=0; i=atoi(id);
        CHECK(i>=0 && i<d->count && !seen[i]); seen[i]=1;
        CHECK_EQ(len,d->len[i]); CHECK(!memcmp(rec,d->rec[i],len)); count++;
    }
    CHECK_EQ(err,RECE_EOF); CHECK_EQ(count,d->count);
    SP_ScanClose(&scan); CHECK_EQ(SP_CloseFile(fd),RECE_OK); free(seen);
}
int main(int argc,char **argv)
{
    int sizes[]={0,1,254,255,508,509,87000};
    int i,j,fd,scan; char name[1024],idx[1100]; StudentData d;
    IX_BuildSpec spec; IX_BuildResult r; IX_TreeInfo t; IX_ValidateResult v;
    CHECK(argc==2); CHECK_EQ(PF_InitWithBufferSize(3),PFE_OK);
    spec.indexNo=0; spec.policy=PF_POLICY_MRU;
    for(i=0;i<(int)(sizeof sizes/sizeof sizes[0]);i++) {
        dataset(&d,sizes[i],0);
        snprintf(name,sizeof name,"%s/edge%d",argv[1],i);
        spec.data=&d; spec.studentFile=name;
        CHECK_EQ(IX_Method3(&spec,&r),IXE_OK);
        CHECK_EQ(r.recordsIndexed,d.count);
        CHECK_EQ(IX_InspectTree(name,0,&t),IXE_OK);
        CHECK_EQ(IX_Validate(name,0,&d,&v),IXE_OK);
        preservation(name,&d);
        if(sizes[i]==87000) CHECK_EQ(t.height,3);
        /* Existing AM insert/delete must also understand bulk pages. */
        IX_IndexFileName(name,0,idx,sizeof idx); fd=PF_OpenFile(idx); CHECK(fd>=0);
        CHECK_EQ(AM_InsertEntry(fd,'c',8,"Z9999999",123456),AME_OK);
        scan=IX_OpenScan(fd,EQUAL,"Z9999999"); CHECK(scan>=0);
        CHECK_EQ(AM_FindNextEntry(scan),123456);
        CHECK_EQ(AM_FindNextEntry(scan),AME_EOF); CHECK_EQ(AM_CloseIndexScan(scan),AME_OK);
        CHECK_EQ(AM_DeleteEntry(fd,'c',8,"Z9999999",123456),AME_OK);
        CHECK_EQ(PF_CloseFile(fd),PFE_OK);
        CHECK_EQ(IX_Validate(name,0,&d,&v),IXE_OK);
        printf("bulk boundary %d: passed\n",sizes[i]);
        IX_RemoveFiles(name,0); SD_Free(&d);
    }
    /* A duplicate list may fill a leaf, but must never spill across leaves. */
    for(j=0;j<2;j++) {
        dataset(&d,j?678:677,1);
        snprintf(name,sizeof name,"%s/duplicates%d",argv[1],j);
        spec.data=&d; spec.studentFile=name;
        if(j) CHECK_EQ(IX_Method3(&spec,&r),IXE_INVALID);
        else {
            CHECK_EQ(IX_Method3(&spec,&r),IXE_OK);
            CHECK_EQ(IX_Validate(name,0,&d,&v),IXE_OK);
        }
        IX_RemoveFiles(name,0); SD_Free(&d);
    }
    puts("bulk edges: all passed"); return 0;
}
