#include "journal_internal.h"
#include <bcrypt.h>
#include <aclapi.h>
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib,"bcrypt.lib")
#define HEADER_SIZE 24u
#define FRAME_SIZE 48u
#define JOURNAL_LIMIT (8u*1024u*1024u)
#define END_MARKER 0xc04d17edu
bool l4_store_fail(DWORD code){SetLastError(code);return false;}
void l4_store_u32(BYTE* out,DWORD v){for(unsigned i=0;i<4;i++)out[i]=(BYTE)(v>>(i*8));}
void l4_store_u64(BYTE* out,ULONGLONG v){for(unsigned i=0;i<8;i++)out[i]=(BYTE)(v>>(i*8));}
DWORD l4_store_get32(const BYTE* b){DWORD v=0;for(unsigned i=0;i<4;i++)v|=(DWORD)b[i]<<(i*8);return v;}
ULONGLONG l4_store_get64(const BYTE* b){ULONGLONG v=0;for(unsigned i=0;i<8;i++)v|=(ULONGLONG)b[i]<<(i*8);return v;}
bool l4_store_hash(const void* bytes,DWORD size,const void* extra,DWORD extra_size,BYTE hash[32]){
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE object=NULL;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&object,NULL,0,NULL,0,0)>=0;
    if(ok && size)ok=BCryptHashData(object,(PUCHAR)bytes,size,0)>=0;
    if(ok && extra_size)ok=BCryptHashData(object,(PUCHAR)extra,extra_size,0)>=0;
    if(ok)ok=BCryptFinishHash(object,hash,32,0)>=0;
    if(object)BCryptDestroyHash(object);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok?true:l4_store_fail(ERROR_CRC);
}
bool l4_store_write(HANDLE file,const void* bytes,DWORD size){DWORD written=0;return WriteFile(file,bytes,size,&written,NULL) && written==size?true:l4_store_fail(ERROR_WRITE_FAULT);}
static bool inside(const wchar_t* root,const wchar_t* path){size_t n=wcslen(root);return !_wcsnicmp(root,path,n) && (!path[n] || path[n]==L'\\');}
bool l4_store_security(HANDLE file,bool control,BYTE** bytes,DWORD* size){
    *bytes=NULL;*size=0;DWORD needed=0;
    GetKernelObjectSecurity(file,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,NULL,0,&needed);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !needed || needed>4096)return l4_store_fail(ERROR_INVALID_SECURITY_DESCR);
    BYTE* sd=(BYTE*)calloc(1,needed);if(!sd)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=GetKernelObjectSecurity(file,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,sd,needed,&needed)!=0;
    PSID owner=NULL;PACL acl=NULL;BOOL present=FALSE,defaulted=FALSE;SECURITY_DESCRIPTOR_CONTROL flags=0;DWORD revision;
    if(ok)ok=GetSecurityDescriptorOwner(sd,&owner,&defaulted) && owner &&
        (IsWellKnownSid(owner,WinLocalSystemSid) || IsWellKnownSid(owner,WinBuiltinAdministratorsSid)) &&
        GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted) && present && acl && acl->AceCount && GetSecurityDescriptorControl(sd,&flags,&revision);
    if(ok && control)ok=(flags&SE_DACL_PROTECTED)!=0;
    for(WORD i=0;ok && i<acl->AceCount;i++){
        ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,i,(void**)&ace) && ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE;if(!ok)break;
        PSID sid=&ace->SidStart;
        if(IsWellKnownSid(sid,WinLocalSystemSid) || IsWellKnownSid(sid,WinBuiltinAdministratorsSid))continue;
        if(control){ok=false;break;}
        DWORD broad_read=FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE;
        bool broad=IsWellKnownSid(sid,WinWorldSid) || IsWellKnownSid(sid,WinBuiltinUsersSid) || IsWellKnownSid(sid,WinAuthenticatedUserSid) || IsWellKnownSid(sid,WinInteractiveSid);
        ok=!(ace->Mask&(WRITE_DAC|WRITE_OWNER|GENERIC_ALL)) && (!broad || !(ace->Mask&~broad_read));
    }
    /* Automatic-inheritance bookkeeping is not part of the persisted policy. */
    if(ok)ok=SetSecurityDescriptorControl(sd,SE_DACL_AUTO_INHERITED|SE_DACL_AUTO_INHERIT_REQ,0)!=0;
    if(!ok){free(sd);return l4_store_fail(ERROR_ACCESS_DENIED);}*bytes=sd;*size=needed;return true;
}
void l4_store_unpin(L4FileFence* fence){while(fence->count)CloseHandle(fence->handles[--fence->count]);}
bool l4_store_pin(const wchar_t* directory,const wchar_t* secure_root,bool control,L4FileFence* fence){
    memset(fence,0,sizeof(*fence));if(!inside(secure_root,directory) || wcslen(directory)>=MAX_PATH)return l4_store_fail(ERROR_INVALID_NAME);
    wchar_t path[MAX_PATH];wcscpy_s(path,MAX_PATH,directory);bool ok=true;DWORD code=0;
    for(wchar_t* p=path+3;;p++)if(!*p || *p==L'\\'){
        wchar_t saved=*p;*p=0;HANDLE h=CreateFileW(path,READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(h==INVALID_HANDLE_VALUE){ok=false;code=GetLastError();}
        else{fence->handles[fence->count++]=h;BY_HANDLE_FILE_INFORMATION info;BYTE* sd=NULL;DWORD size;
            ok=GetFileInformationByHandle(h,&info)!=0 && (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
            if(ok && inside(secure_root,path))ok=l4_store_security(h,control,&sd,&size);free(sd);if(!ok)code=GetLastError()?GetLastError():ERROR_ACCESS_DENIED;}
        *p=saved;if(!ok || !saved)break;
    }
    if(!ok){l4_store_unpin(fence);return l4_store_fail(code);}return true;
}
/* Reader-only helper links file-security primitives, never journal replay/write. */
#ifndef L4_RECOVERY_READER_ONLY
static bool safe_file(HANDLE file){BY_HANDLE_FILE_INFORMATION info;BYTE* sd=NULL;DWORD size;
    bool ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 && l4_store_security(file,true,&sd,&size);free(sd);return ok?true:l4_store_fail(ERROR_ACCESS_DENIED);}
static bool seek(HANDLE file,ULONGLONG offset){LARGE_INTEGER pos;pos.QuadPart=(LONGLONG)offset;return SetFilePointerEx(file,pos,NULL,FILE_BEGIN)!=0;}
static bool scan(L4Journal* j,L4JournalVisitor visit,void* context,bool repair){
    LARGE_INTEGER size={0};if(!GetFileSizeEx(j->file,&size) || size.QuadPart<HEADER_SIZE || size.QuadPart>JOURNAL_LIMIT)return l4_store_fail(ERROR_INVALID_DATA);
    BYTE* data=(BYTE*)malloc((size_t)size.QuadPart);if(!data)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);
    DWORD read=0;bool ok=seek(j->file,0) && ReadFile(j->file,data,(DWORD)size.QuadPart,&read,NULL) && read==(DWORD)size.QuadPart;
    if(ok && memcmp(data,j->header,HEADER_SIZE)){ok=false;SetLastError(ERROR_INVALID_DATA);}
    BYTE digest[32];if(ok)ok=l4_store_hash(data,HEADER_SIZE,NULL,0,digest);
    ULONGLONG sequence=0;DWORD end=HEADER_SIZE;
    while(ok && end<(DWORD)size.QuadPart){DWORD remaining=(DWORD)size.QuadPart-end;if(remaining<FRAME_SIZE)break;
        const BYTE* frame=data+end;DWORD kind=l4_store_get32(frame),length=l4_store_get32(frame+4);ULONGLONG number=l4_store_get64(frame+8);
        if(!kind || length>L4_JOURNAL_MAX_RECORD || number!=sequence+1){ok=false;SetLastError(ERROR_INVALID_DATA);break;}
        if(remaining<FRAME_SIZE+length+4)break;
        BYTE prefix[48],actual[32];memcpy(prefix,frame,16);memcpy(prefix+16,digest,32);
        ok=l4_store_get32(frame+FRAME_SIZE+length)==END_MARKER && l4_store_hash(prefix,sizeof(prefix),frame+FRAME_SIZE,length,actual) && !memcmp(actual,frame+16,32);
        if(!ok){SetLastError(ERROR_CRC);break;}
        if(!ok)break;memcpy(digest,actual,32);sequence=number;end+=FRAME_SIZE+length+4;
    }
    if(ok && repair && end!=(DWORD)size.QuadPart)ok=seek(j->file,end) && SetEndOfFile(j->file) && FlushFileBuffers(j->file);
    if(ok && repair){j->sequence=sequence;j->end=end;memcpy(j->digest,digest,32);}
    /* Validate the entire committed prefix before exposing any record. */
    if(ok && visit){
        DWORD at=HEADER_SIZE;
        while(ok && at<end){const BYTE* frame=data+at;DWORD length=l4_store_get32(frame+4);
            ok=visit(l4_store_get32(frame),l4_store_get64(frame+8),frame+FRAME_SIZE,length,context);
            at+=FRAME_SIZE+length+4;
        }
    }
    DWORD code=GetLastError();free(data);return ok?true:l4_store_fail(code);
}
void l4_journal_close(L4Journal* j){if(!j)return;if(j->file && j->file!=INVALID_HANDLE_VALUE)CloseHandle(j->file);if(j->lock && j->lock!=INVALID_HANDLE_VALUE)CloseHandle(j->lock);l4_store_unpin(&j->fence);free(j);}
bool l4_journal_open(const L4Layout* layout,const wchar_t* id,bool create,L4Journal** result){
    if(!result)return l4_store_fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!id || wcslen(id)!=36)return l4_store_fail(ERROR_INVALID_NAME);
    for(unsigned i=0;i<36;i++){bool dash=i==8 || i==13 || i==18 || i==23;if(dash?(id[i]!=L'-'):!((id[i]>=L'0'&&id[i]<=L'9')||(id[i]>=L'a'&&id[i]<=L'f')||(id[i]>=L'A'&&id[i]<=L'F')))return l4_store_fail(ERROR_INVALID_NAME);}
    wchar_t guid_text[40];swprintf_s(guid_text,40,L"{%ls}",id);GUID guid,zero={0};if(FAILED(CLSIDFromString(guid_text,&guid)) || !memcmp(&guid,&zero,sizeof(guid)))return l4_store_fail(ERROR_INVALID_NAME);
    wchar_t path[MAX_PATH],relative[128];if(!l4_layout_data_path(layout,L"update\\operations\\deployment.lock",path))return false;
    L4Journal* j=(L4Journal*)calloc(1,sizeof(*j));if(!j)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);j->layout=*layout;j->file=j->lock=INVALID_HANDLE_VALUE;
    L4FileFence root={0};bool ok=l4_store_pin(layout->operations,layout->data,false,&root);
    PSECURITY_DESCRIPTOR sd=NULL;
    if(ok)ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",SDDL_REVISION_1,&sd,NULL)!=0;
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),sd,FALSE};
    if(ok){j->lock=CreateFileW(path,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,&attributes,create?OPEN_ALWAYS:OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=j->lock!=INVALID_HANDLE_VALUE && safe_file(j->lock);}
    swprintf_s(relative,_countof(relative),L"update\\operations\\%ls",id);
    if(ok)ok=l4_layout_data_path(layout,relative,j->directory);
    if(ok && create && !CreateDirectoryW(j->directory,&attributes) && GetLastError()!=ERROR_ALREADY_EXISTS)ok=false;
    if(ok)ok=l4_store_pin(j->directory,j->directory,true,&j->fence);
    if(ok && wcslen(j->directory)+13>=MAX_PATH){ok=false;SetLastError(ERROR_FILENAME_EXCED_RANGE);}
    if(ok){swprintf_s(path,MAX_PATH,L"%ls\\journal.bin",j->directory);j->file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ,&attributes,create?OPEN_ALWAYS:OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,NULL);ok=j->file!=INVALID_HANDLE_VALUE && safe_file(j->file);}
    BYTE header[HEADER_SIZE];memcpy(header,"L4J1",4);l4_store_u32(header+4,1);memcpy(header+8,&guid,16);LARGE_INTEGER size={0};
    if(ok)ok=GetFileSizeEx(j->file,&size)!=0;
    if(ok && !size.QuadPart && create)ok=l4_store_write(j->file,header,sizeof(header)) && FlushFileBuffers(j->file);
    if(ok){BYTE actual[HEADER_SIZE];DWORD read=0;ok=seek(j->file,0) && ReadFile(j->file,actual,sizeof(actual),&read,NULL) && read==sizeof(actual) && !memcmp(actual,header,sizeof(header));if(!ok)SetLastError(ERROR_INVALID_DATA);}
    if(ok){memcpy(j->header,header,sizeof(header));ok=scan(j,NULL,NULL,true);}
    DWORD code=GetLastError();LocalFree(sd);l4_store_unpin(&root);if(!ok){l4_journal_close(j);return l4_store_fail(code);}*result=j;return true;
}
bool l4_journal_append(L4Journal* j,DWORD kind,const void* bytes,DWORD size,ULONGLONG* sequence){
    if(!j || j->poisoned || !kind || size>L4_JOURNAL_MAX_RECORD || (size&&!bytes))return l4_store_fail(ERROR_INVALID_PARAMETER);
    if(j->end+FRAME_SIZE+size+4>JOURNAL_LIMIT)return l4_store_fail(ERROR_DISK_FULL);
    BYTE frame[FRAME_SIZE],prefix[48],marker[4];l4_store_u32(frame,kind);l4_store_u32(frame+4,size);l4_store_u64(frame+8,j->sequence+1);
    memcpy(prefix,frame,16);memcpy(prefix+16,j->digest,32);l4_store_u32(marker,END_MARKER);
    bool ok=l4_store_hash(prefix,sizeof(prefix),bytes,size,frame+16) && seek(j->file,j->end) &&
        l4_store_write(j->file,frame,sizeof(frame)) && l4_store_write(j->file,bytes,size) && l4_store_write(j->file,marker,sizeof(marker)) && FlushFileBuffers(j->file);
    if(!ok){j->poisoned=true;return false;}++j->sequence;j->end+=FRAME_SIZE+size+4;memcpy(j->digest,frame+16,32);if(sequence)*sequence=j->sequence;return true;
}
bool l4_journal_replay(L4Journal* j,L4JournalVisitor visit,void* context){if(!j || j->poisoned || !visit)return l4_store_fail(ERROR_INVALID_PARAMETER);return scan(j,visit,context,false);}
typedef struct{DWORD kind;ULONGLONG sequence;BYTE* bytes;DWORD size;} Found;
static bool find_record(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){Found* f=(Found*)context;if(kind==f->kind && sequence==f->sequence){f->bytes=(BYTE*)malloc(size?size:1);if(!f->bytes)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);memcpy(f->bytes,bytes,size);f->size=size;}return true;}
bool l4_store_find_record(L4Journal* j,DWORD kind,ULONGLONG sequence,BYTE** bytes,DWORD* size){Found f={kind,sequence,NULL,0};if(!l4_journal_replay(j,find_record,&f)){free(f.bytes);return false;}if(!f.bytes)return l4_store_fail(ERROR_NOT_FOUND);*bytes=f.bytes;*size=f.size;return true;}

#endif /* L4_RECOVERY_READER_ONLY */
