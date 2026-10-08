#include "../catalog_internal.h"
#include "../route_plan.h"
#include "../journal_internal.h"
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL catalog %u: %s (error=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static DWORD load(const wchar_t* dir,const wchar_t* name,BYTE* bytes,DWORD capacity){
    wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls\\%ls",dir,name);FILE* file=NULL;CHECK(_wfopen_s(&file,path,L"rb")==0 && file);if(!file)return 0;
    size_t size=fread(bytes,1,capacity,file);CHECK(!ferror(file) && feof(file));fclose(file);return (DWORD)size;
}
static L4Catalog* read_catalog(const wchar_t* directory,const wchar_t* name,const BYTE* key,DWORD key_size,const char* identity,ULONGLONG now,bool expected){
    BYTE bytes[65536],sig[385];wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls.json",name);DWORD size=load(directory,path,bytes,sizeof(bytes));
    swprintf_s(path,MAX_PATH,L"%ls.json.sig",name);DWORD sig_size=load(directory,path,sig,sizeof(sig));L4Catalog* c=NULL;
    CHECK(l4_catalog_parse_signed(bytes,size,sig,sig_size,key,key_size,identity,now,&c)==expected);CHECK((c!=NULL)==expected);
    if(expected){L4Catalog* other=NULL;CHECK(!l4_catalog_parse_trusted(bytes,size,sig,sig_size,now,&other));CHECK(!other);
        bytes[0]^=1;CHECK(!l4_catalog_parse_signed(bytes,size,sig,sig_size,key,key_size,identity,now,&other));CHECK(!other);}
    return c;
}
static void cleanup(const wchar_t* root){wchar_t pattern[MAX_PATH],child[MAX_PATH];swprintf_s(pattern,MAX_PATH,L"%ls\\*",root);WIN32_FIND_DATAW d;HANDLE h=FindFirstFileW(pattern,&d);
    if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(d.cFileName,L".")||!wcscmp(d.cFileName,L".."))continue;swprintf_s(child,MAX_PATH,L"%ls\\%ls",root,d.cFileName);
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))cleanup(child);else RemoveDirectoryW(child);}else DeleteFileW(child);
    }while(FindNextFileW(h,&d));FindClose(h);}RemoveDirectoryW(root);}
static bool append(const wchar_t* path,const void* bytes,DWORD size){HANDLE h=CreateFileW(path,FILE_APPEND_DATA,0,NULL,OPEN_EXISTING,0,NULL);if(h==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;bool ok=WriteFile(h,bytes,size,&written,NULL) && written==size && FlushFileBuffers(h);CloseHandle(h);return ok;}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 1;BYTE key[412],identity_bytes[66];DWORD key_size=load(argv[1],L"public.blob",key,sizeof(key));
    DWORD identity_size=load(argv[1],L"key-id.txt",identity_bytes,sizeof(identity_bytes)-1);identity_bytes[identity_size]=0;const char* identity=(const char*)identity_bytes;
    L4Catalog* c=read_catalog(argv[1],L"catalog",key,key_size,identity,150,true);if(!c)return 1;
    BYTE current[32];memset(current,0x11,32);L4CatalogRoute route;
    L4CatalogRelease resolved;CHECK(l4_catalog_resolve(c,"latest",&resolved));CHECK(!strcmp(resolved.version,"1.2.0"));
    CHECK(!l4_catalog_resolve(c,"2.0.0",&resolved));CHECK(!resolved.version[0]);
    CHECK(l4_catalog_route(c,"1.0.0",current,"latest","x86","windows-10-x64",&route));CHECK(route.count==2);
    CHECK(!strcmp(route.releases[0].version,"1.1.0") && !strcmp(route.releases[1].version,"1.2.0"));CHECK(route.releases[1].manifest_sha256[0]==0x33);
    CHECK(l4_catalog_route(c,"1.0.0",current,"latest","x64","windows-10-x64",&route));CHECK(route.count==1);
    CHECK(l4_catalog_route(c,"1.0.0",current,"1.0.0","x86","windows-10-x64",&route));CHECK(route.count==0);
    CHECK(!l4_catalog_route(c,"1.0.0",current,"latest","x86","windows-7-x86",&route));CHECK(route.count==0);
    current[0]^=1;CHECK(!l4_catalog_route(c,"1.0.0",current,"latest","x86","windows-10-x64",&route));current[0]^=1;
    c->releases[1].revoked=true;CHECK(!l4_catalog_route(c,"1.0.0",current,"latest","x86","windows-10-x64",&route));c->releases[1].revoked=false;
    int stable=c->stable;c->stable=-1;CHECK(!l4_catalog_route(c,"1.0.0",current,"latest","x64","windows-10-x64",&route));c->stable=stable;
    for(unsigned i=0;i<18;i++){wchar_t name[32];swprintf_s(name,32,L"bad-%u",i);l4_catalog_free(read_catalog(argv[1],name,key,key_size,identity,150,false));}
    L4Catalog* maximum=read_catalog(argv[1],L"maximum",key,key_size,identity,150,true);
    if(maximum){CHECK(maximum->release_count==24 && maximum->transition_count==24);
        CHECK(l4_catalog_route(maximum,"1.0.0",current,"latest","x86","windows-10-x64",&route));CHECK(route.count==23);l4_catalog_free(maximum);}
    l4_catalog_free(read_catalog(argv[1],L"catalog",key,key_size,identity,99,false));l4_catalog_free(read_catalog(argv[1],L"catalog",key,key_size,identity,200,false));
    wchar_t temp[MAX_PATH],root[MAX_PATH],programs[MAX_PATH],data[MAX_PATH],floor[MAX_PATH],alias[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH,temp));swprintf_s(root,MAX_PATH,L"%lsl4catalog-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());CHECK(CreateDirectoryW(root,NULL));
    swprintf_s(programs,MAX_PATH,L"%ls\\Programs",root);swprintf_s(data,MAX_PATH,L"%ls\\Data",root);L4Layout layout;
    CHECK(l4_layout_from_roots(&layout,programs,data,L"1.2.0"));CHECK(l4_layout_prepare(&layout));
    L4Journal *j=NULL,*other=NULL;CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000001",true,&j));
    if(j){
        CHECK(l4_catalog_accept(j,c,150));CHECK(l4_catalog_accept(j,c,150));CHECK(!l4_catalog_accept(j,c,149));
        BYTE doc[65536],sig[385];DWORD doc_size=load(argv[1],L"catalog.json",doc,sizeof(doc)),sig_size=load(argv[1],L"catalog.json.sig",sig,sizeof(sig));ULONGLONG saved=0;
        CHECK(!l4_route_save_trusted(j,doc,doc_size,sig,sig_size,150,"1.0.0",current,"latest","x86","windows-10-x64",&saved));CHECK(!saved);
        CHECK(!l4_route_save_signed(j,doc,doc_size,sig,sig_size,key,key_size,identity,150,"1.0.0",current,"latest","x86","windows-7-x86",&saved));CHECK(!saved);
        CHECK(l4_route_save_signed(j,doc,doc_size,sig,sig_size,key,key_size,identity,150,"1.0.0",current,"latest","x86","windows-10-x64",&saved));CHECK(saved==1);
        L4RoutePlan* plan=NULL;CHECK(l4_route_load_signed(j,saved,key,key_size,identity,&plan));
        if(plan){CHECK(!l4_route_is_owner_trusted(plan));CHECK(l4_route_steps(plan)->count==2);CHECK(!strcmp(l4_route_steps(plan)->releases[1].version,"1.2.0"));
            CHECK(!strcmp(l4_route_requested(plan),"latest") && !strcmp(l4_route_arch(plan),"x86") && !strcmp(l4_route_profile(plan),"windows-10-x64"));
            CHECK(l4_route_revision(plan)==8 && l4_route_admitted_at(plan)==150);CHECK(!memcmp(l4_route_catalog_sha256(plan),c->digest,32));
            CHECK(l4_route_source(plan,&resolved) && !strcmp(resolved.version,"1.0.0") && !memcmp(resolved.manifest_sha256,current,32));
            CHECK(l4_route_evidence(plan,0) && l4_route_evidence(plan,1) && !l4_route_evidence(plan,2));l4_route_free(plan);plan=NULL;}
        CHECK(!l4_route_load_trusted(j,saved,&plan));CHECK(!plan);
        BYTE* encoded=NULL;DWORD encoded_size=0;CHECK(l4_store_find_record(j,L4_RECORD_ROUTE_PLAN,saved,&encoded,&encoded_size));
        if(encoded){
            CHECK(l4_route_decode_signed(encoded,encoded_size,key,key_size,identity,&plan));
            if(plan){CHECK(!l4_route_is_owner_trusted(plan) && l4_route_steps(plan)->count==2 && l4_route_admitted_at(plan)==150);l4_route_free(plan);plan=NULL;}
            CHECK(!l4_route_decode_trusted(encoded,encoded_size,&plan) && !plan);
            CHECK(!l4_route_decode_signed(encoded,encoded_size-1,key,key_size,identity,&plan) && !plan);
            encoded[0]^=1;CHECK(!l4_route_decode_signed(encoded,encoded_size,key,key_size,identity,&plan) && !plan);encoded[0]^=1;
            encoded[encoded_size-1]^=1;CHECK(!l4_route_decode_signed(encoded,encoded_size,key,key_size,identity,&plan) && !plan);encoded[encoded_size-1]^=1;
            CHECK(l4_route_decode_signed(encoded,encoded_size,key,key_size,identity,&plan));l4_route_free(plan);plan=NULL;free(encoded);
        }
        key[410]^=2;CHECK(!l4_route_load_signed(j,saved,key,key_size,identity,&plan));CHECK(!plan);key[410]^=2;
        CHECK(!l4_route_load_signed(j,999,key,key_size,identity,&plan));CHECK(!plan);
        ULONGLONG duplicate=0;CHECK(!l4_route_save_signed(j,doc,doc_size,sig,sig_size,key,key_size,identity,151,"1.0.0",current,"1.1.0","x86","windows-10-x64",&duplicate));CHECK(!duplicate);
        CHECK(!l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000002",true,&other));CHECK(!other);
        HANDLE token=NULL;CHECK(!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token) && GetLastError()==ERROR_NO_TOKEN);if(token)CloseHandle(token);
        l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000001",false,&j));
        CHECK(l4_route_load_signed(j,saved,key,key_size,identity,&plan));if(plan){CHECK(l4_route_steps(plan)->count==2);l4_route_free(plan);plan=NULL;}
        l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000002",true,&j));
        L4Catalog* old=read_catalog(argv[1],L"older",key,key_size,identity,151,true);if(old){CHECK(!l4_catalog_accept(j,old,151));l4_catalog_free(old);}
        L4Catalog* different=read_catalog(argv[1],L"different",key,key_size,identity,151,true);if(different){CHECK(!l4_catalog_accept(j,different,151));l4_catalog_free(different);}
        CHECK(l4_layout_data_path(&layout,L"state\\catalog.floor",floor));
        HANDLE held=CreateFileW(floor,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(held!=INVALID_HANDLE_VALUE);
        CHECK(!l4_catalog_accept(j,c,151));if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
        swprintf_s(alias,MAX_PATH,L"%ls\\alias.floor",data);CHECK(CreateHardLinkW(alias,floor,NULL));CHECK(!l4_catalog_accept(j,c,151));CHECK(DeleteFileW(alias));
        swprintf_s(alias,MAX_PATH,L"%ls:extra",floor);HANDLE stream=CreateFileW(alias,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(stream!=INVALID_HANDLE_VALUE);if(stream!=INVALID_HANDLE_VALUE)CloseHandle(stream);
        CHECK(!l4_catalog_accept(j,c,151));CHECK(DeleteFileW(alias));CHECK(l4_catalog_accept(j,c,151));
        L4Catalog* high=read_catalog(argv[1],L"higher",key,key_size,identity,152,true);if(high){CHECK(l4_catalog_accept(j,high,152));CHECK(!l4_catalog_accept(j,c,153));l4_catalog_free(high);}
        CHECK(!l4_route_save_signed(j,doc,doc_size,sig,sig_size,key,key_size,identity,201,"1.0.0",current,"latest","x86","windows-10-x64",&duplicate));CHECK(!duplicate);
        CHECK(!l4_route_save_signed(j,doc,doc_size,sig,sig_size,key,key_size,identity,153,"1.0.0",current,"latest","x86","windows-10-x64",&duplicate));CHECK(!duplicate);
        /* Existing selection survives newer global floor and expiry: load requires
         * original acknowledged operation, not a new catalog admission. */
        l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&layout,L"17730000-0000-4000-8000-000000000001",false,&j));
        CHECK(l4_route_load_signed(j,saved,key,key_size,identity,&plan));if(plan){CHECK(l4_route_admitted_at(plan)==150);l4_route_free(plan);plan=NULL;}
        BYTE bad[16]={0};CHECK(l4_journal_append(j,L4_RECORD_ROUTE_PLAN,bad,sizeof(bad),NULL));
        CHECK(!l4_route_load_signed(j,saved,key,key_size,identity,&plan));CHECK(!plan);
        CHECK(append(floor,"torn",4));CHECK(!l4_catalog_accept(j,c,154));
    }
    l4_journal_close(j);l4_journal_close(other);l4_catalog_free(c);cleanup(root);CHECK(GetFileAttributesW(root)==INVALID_FILE_ATTRIBUTES);
    printf("Signed catalog/route/global floor: %u checks, %u failures; real protected I/O, no services\n",checks,failures);return failures?1:0;
}
