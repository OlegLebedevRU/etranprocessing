#include "../catalog_floor.h"
#include "../metadata.h"
#include "../journal_internal.h"
#include "update_state_fixture.h"
#include <sddl.h>
#include <stdio.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL floor:%u %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool put(const wchar_t* path,const void* bytes,DWORD n){PSECURITY_DESCRIPTOR sd=NULL;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL))return false;
 HANDLE previous=NULL;bool scoped=l4_layout_owner_begin(&previous);SECURITY_ATTRIBUTES a={sizeof(a),sd,FALSE};HANDLE file=scoped?CreateFileW(path,GENERIC_WRITE,0,&a,CREATE_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,NULL):INVALID_HANDLE_VALUE;bool ok=file!=INVALID_HANDLE_VALUE;DWORD written=0;
 if(ok)ok=WriteFile(file,bytes,n,&written,NULL)&&written==n&&FlushFileBuffers(file);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(scoped&&!l4_layout_owner_end(previous))ok=false;LocalFree(sd);return ok;
}
static void frame(BYTE* bytes,DWORD at,ULONGLONG revision,ULONGLONG time,const BYTE previous[32]){l4_store_u64(bytes+at,revision);l4_store_u64(bytes+at+8,time);memset(bytes+at+16,1,32);CHECK(l4_store_hash(bytes+at,48,previous,32,bytes+at+48));}
int main(void){UpdateFixture fixture;CHECK(update_fixture_init(&fixture));wchar_t path[MAX_PATH],alias[MAX_PATH],stream[MAX_PATH];CHECK(l4_layout_data_path(&fixture.layout,L"state\\catalog.floor",path));
 CHECK(l4_catalog_floor_verify_existing(&fixture.layout));CHECK(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
 BYTE bytes[232]={0},header_digest[32];memcpy(bytes,"L4CAT01\0",8);memcpy(bytes+8,l4_metadata_key_id(),64);CHECK(l4_store_hash(bytes,72,NULL,0,header_digest));frame(bytes,72,1,123,header_digest);
 CHECK(put(path,bytes,152));CHECK(l4_catalog_floor_verify_existing(&fixture.layout));CHECK(l4_catalog_floor_verify_existing(&fixture.layout));BYTE actual[232];HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);DWORD n=0;CHECK(file!=INVALID_HANDLE_VALUE&&ReadFile(file,actual,sizeof(actual),&n,NULL)&&n==152&&!memcmp(actual,bytes,152));if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
 frame(bytes,152,1,124,bytes+120);CHECK(put(path,bytes,232));CHECK(l4_catalog_floor_verify_existing(&fixture.layout));
 frame(bytes,152,0,125,bytes+120);CHECK(put(path,bytes,232));CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));
 frame(bytes,152,1,122,bytes+120);CHECK(put(path,bytes,232));CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));
 frame(bytes,152,1,125,bytes+120);bytes[168]=2;CHECK(l4_store_hash(bytes+152,48,bytes+120,32,bytes+200));CHECK(put(path,bytes,232));CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));
 CHECK(put(path,bytes,153));CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));bytes[8]^=1;CHECK(put(path,bytes,152));CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));bytes[8]^=1;bytes[151]^=1;CHECK(put(path,bytes,152));CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));bytes[151]^=1;CHECK(put(path,bytes,152));CHECK(l4_catalog_floor_verify_existing(&fixture.layout));
 swprintf_s(alias,MAX_PATH,L"%ls\\floor-alias",fixture.layout.data);CHECK(CreateHardLinkW(alias,path,NULL));CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));CHECK(DeleteFileW(alias));
 swprintf_s(stream,MAX_PATH,L"%ls:extra",path);file=CreateFileW(stream,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));CHECK(DeleteFileW(stream));
 file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);CHECK(file!=INVALID_HANDLE_VALUE);CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);CHECK(l4_catalog_floor_verify_existing(&fixture.layout));
 PSECURITY_DESCRIPTOR sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GR;;;WD)",SDDL_REVISION_1,&sd,NULL));if(sd){CHECK(SetFileSecurityW(path,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd));LocalFree(sd);}CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));
 sd=NULL;CHECK(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL));if(sd){CHECK(SetFileSecurityW(path,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd));LocalFree(sd);}CHECK(l4_catalog_floor_verify_existing(&fixture.layout));
 CHECK(DeleteFileW(path));CHECK(CreateDirectoryW(path,NULL));CHECK(!l4_catalog_floor_verify_existing(&fixture.layout));CHECK(RemoveDirectoryW(path));CHECK(l4_catalog_floor_verify_existing(&fixture.layout));CHECK(update_fixture_dispose(&fixture));
 printf("Catalog floor read-only preservation: %u checks, %u failures; real protected I/O, no services\n",checks,failures);return failures?1:0;}
