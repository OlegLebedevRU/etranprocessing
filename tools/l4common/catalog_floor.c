#include "catalog_internal.h"
#include "journal_internal.h"
#include "catalog_floor.h"
#include "metadata.h"
#include <sddl.h>
#include <stdlib.h>
#include <string.h>
#define FLOOR_HEADER 72u
#define FLOOR_FRAME 80u
#define FLOOR_LIMIT (8u*1024u*1024u)
static bool no_streams(const wchar_t* path){
    WIN32_FIND_STREAM_DATA data;HANDLE h=FindFirstStreamW(path,FindStreamInfoStandard,&data,0);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_HANDLE_EOF;
    bool ok=true;do{if(wcscmp(data.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(h,&data));
    DWORD code=GetLastError();FindClose(h);return ok && code==ERROR_HANDLE_EOF;
}
typedef struct{LARGE_INTEGER size;BYTE previous_hash[32],last_digest[32];ULONGLONG revision,time;} FloorState;
static bool read_floor(HANDLE file,const char* key_id,FloorState* state){
    memset(state,0,sizeof(*state));if(!key_id || strlen(key_id)!=64)return l4_store_fail(ERROR_INVALID_DATA);
    BYTE header[FLOOR_HEADER]={0},actual_header[FLOOR_HEADER];memcpy(header,"L4CAT01\0",8);memcpy(header+8,key_id,64);DWORD read=0;LARGE_INTEGER position={0};
    bool ok=GetFileSizeEx(file,&state->size) && state->size.QuadPart>=FLOOR_HEADER && state->size.QuadPart<=FLOOR_LIMIT && (state->size.QuadPart-FLOOR_HEADER)%FLOOR_FRAME==0 &&
        SetFilePointerEx(file,position,NULL,FILE_BEGIN) && ReadFile(file,actual_header,sizeof(actual_header),&read,NULL) && read==sizeof(actual_header) &&
        !memcmp(header,actual_header,sizeof(header)) && l4_store_hash(header,sizeof(header),NULL,0,state->previous_hash);
    for(LONGLONG offset=FLOOR_HEADER;ok && offset<state->size.QuadPart;offset+=FLOOR_FRAME){
        BYTE frame[FLOOR_FRAME],digest[32];ok=ReadFile(file,frame,sizeof(frame),&read,NULL) && read==sizeof(frame) &&
            l4_store_hash(frame,48,state->previous_hash,32,digest) && !memcmp(digest,frame+48,32);
        if(!ok)break;ULONGLONG next_revision=l4_store_get64(frame),next_time=l4_store_get64(frame+8);
        ok=next_revision>0 && next_revision>=state->revision && next_time>=state->time &&
            (next_revision!=state->revision || !memcmp(frame+16,state->last_digest,32));
        if(ok){state->revision=next_revision;state->time=next_time;memcpy(state->last_digest,frame+16,32);memcpy(state->previous_hash,digest,32);}
    }
    return ok?true:l4_store_fail(ERROR_INVALID_DATA);
}
bool l4_catalog_floor_verify_existing(const L4Layout* layout){
    if(!layout)return l4_store_fail(ERROR_INVALID_PARAMETER);wchar_t path[MAX_PATH];L4FileFence fence={0};
    if(!l4_layout_data_path(layout,L"state\\catalog.floor",path) || !l4_store_pin(layout->state,layout->data,false,&fence))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    DWORD error=GetLastError();bool ok=file!=INVALID_HANDLE_VALUE;
    if(!ok && error==ERROR_FILE_NOT_FOUND){l4_store_unpin(&fence);return true;}
    BYTE* security=NULL;DWORD n=0;BY_HANDLE_FILE_INFORMATION info;FloorState state;
    if(ok)ok=GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
        l4_store_security(file,true,&security,&n) && no_streams(path) && read_floor(file,l4_metadata_key_id(),&state);
    error=GetLastError();free(security);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);l4_store_unpin(&fence);return ok?true:l4_store_fail(error?error:ERROR_INVALID_DATA);
}
bool l4_catalog_accept(L4Journal* j,const L4Catalog* c,ULONGLONG now){
    if(!j || !c || j->poisoned || j->lock==INVALID_HANDLE_VALUE || !now || now<c->issued || now>=c->expires)return l4_store_fail(ERROR_INVALID_PARAMETER);
    wchar_t path[MAX_PATH];if(!l4_layout_data_path(&j->layout,L"state\\catalog.floor",path))return false;
    L4FileFence fence={0};if(!l4_store_pin(j->layout.state,j->layout.data,false,&fence))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    bool created=false,ok=file!=INVALID_HANDLE_VALUE;DWORD code=GetLastError();
    if(!ok && code==ERROR_FILE_NOT_FOUND){
        PSECURITY_DESCRIPTOR sd=NULL;HANDLE previous=NULL;bool scoped=false;
        ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&sd,NULL)!=0;
        if(ok){scoped=l4_layout_owner_begin(&previous);ok=scoped;}
        if(ok){SECURITY_ATTRIBUTES attrs={sizeof(attrs),sd,FALSE};file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,&attrs,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
            ok=file!=INVALID_HANDLE_VALUE;code=GetLastError();created=ok;}
        if(scoped && !l4_layout_owner_end(previous)){ok=false;code=GetLastError();}if(sd)LocalFree(sd);
    }
    BYTE* security=NULL;DWORD security_size=0;BY_HANDLE_FILE_INFORMATION info={0};LARGE_INTEGER size={0};
    if(ok)ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 &&
        l4_store_security(file,true,&security,&security_size) && no_streams(path) && GetFileSizeEx(file,&size);
    free(security);
    BYTE header[FLOOR_HEADER]={0};memcpy(header,"L4CAT01\0",8);memcpy(header+8,c->key_id,64);
    if(ok && created){ok=l4_store_write(file,header,sizeof(header)) && FlushFileBuffers(file);size.QuadPart=sizeof(header);if(!ok)j->poisoned=true;}
    FloorState state={0};if(ok)ok=read_floor(file,c->key_id,&state);
    if(ok && (c->revision<state.revision || now<state.time || (c->revision==state.revision && memcmp(c->digest,state.last_digest,32)))){ok=false;code=ERROR_REVISION_MISMATCH;}
    if(ok && !(c->revision==state.revision && now==state.time)){
        BYTE frame[FLOOR_FRAME];l4_store_u64(frame,c->revision);l4_store_u64(frame+8,now);memcpy(frame+16,c->digest,32);
        ok=state.size.QuadPart+FLOOR_FRAME<=FLOOR_LIMIT && l4_store_hash(frame,48,state.previous_hash,32,frame+48);
        if(ok){ok=l4_store_write(file,frame,sizeof(frame)) && FlushFileBuffers(file);if(!ok)j->poisoned=true;}
    }
    if(!ok && code!=ERROR_REVISION_MISMATCH)code=GetLastError()?GetLastError():ERROR_INVALID_DATA;
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);l4_store_unpin(&fence);return ok?true:l4_store_fail(code);
}
