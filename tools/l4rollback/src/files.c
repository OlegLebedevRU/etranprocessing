#include "rollback.h"
#include "../../l4common/journal_internal.h"
#include <aclapi.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bcrypt.h>

static bool fail(DWORD error){SetLastError(error);return false;}
static bool no_streams(const wchar_t* name){
    WIN32_FIND_STREAM_DATA data;HANDLE h=FindFirstStreamW(name,FindStreamInfoStandard,&data,0);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_HANDLE_EOF;
    bool ok=true;do{if(wcscmp(data.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(h,&data));
    DWORD error=GetLastError();FindClose(h);return ok && error==ERROR_HANDLE_EOF;
}
static bool read_policy(HANDLE h,BYTE** sd,DWORD* size){
    if(!l4_store_security(h,false,sd,size))return false;
    PACL acl=NULL;BOOL present,defaulted;bool ok=GetSecurityDescriptorDacl(*sd,&present,&acl,&defaulted) && present && acl;
    for(WORD i=0;ok && i<acl->AceCount;i++){
        ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,i,(void**)&ace)!=0;if(!ok)break;
        PSID sid=&ace->SidStart;
        if(!IsWellKnownSid(sid,WinLocalSystemSid) && !IsWellKnownSid(sid,WinBuiltinAdministratorsSid))
            ok=!(ace->Mask&~(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE));
    }
    return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool tail_policy(const L4FileFence* fence,unsigned tail){
    if(fence->count<tail)return fail(ERROR_INVALID_DATA);
    for(unsigned i=fence->count-tail;i<fence->count;i++){
        BYTE* sd=NULL;DWORD size;bool ok=read_policy(fence->handles[i],&sd,&size);free(sd);if(!ok)return false;
    }return true;
}
static bool regular(HANDLE h,const wchar_t* name,DWORD limit,DWORD* size){
    BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER length={0};
    if(!GetFileInformationByHandle(h,&info) || info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY) || info.nNumberOfLinks!=1 ||
       !GetFileSizeEx(h,&length) || length.QuadPart<0 || length.QuadPart>limit || !no_streams(name))return fail(ERROR_INVALID_DATA);
    *size=(DWORD)length.QuadPart;return true;
}
bool rollback_installed(const L4Layout* roots,const wchar_t* image){
    wchar_t parent[MAX_PATH];L4FileFence fence={0};wcscpy_s(parent,MAX_PATH,image);wchar_t* slash=wcsrchr(parent,L'\\');if(!slash)return fail(ERROR_INVALID_NAME);*slash=0;
    bool ok=l4_store_pin(parent,roots->binaries,false,&fence) && tail_policy(&fence,2);HANDLE h=INVALID_HANDLE_VALUE;BYTE* sd=NULL;DWORD size=0,sd_size=0;
    if(ok){h=CreateFileW(image,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=h!=INVALID_HANDLE_VALUE && regular(h,image,MAXDWORD,&size) && size && read_policy(h,&sd,&sd_size);}
    DWORD error=GetLastError();free(sd);if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);l4_store_unpin(&fence);return ok?true:fail(error?error:ERROR_ACCESS_DENIED);
}
static bool image_hash(HANDLE file,DWORD size,const BYTE expected[32]){
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE buffer[65536],digest[32];DWORD total=0;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0 && BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)>=0;
    while(ok && total<size){DWORD count=0,want=size-total;if(want>sizeof(buffer))want=sizeof(buffer);
        ok=ReadFile(file,buffer,want,&count,NULL) && count==want && BCryptHashData(hash,buffer,count,0)>=0;total+=count;}
    if(ok)ok=BCryptFinishHash(hash,digest,32,0)>=0 && !memcmp(digest,expected,32);
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok?true:fail(ERROR_CRC);
}
DWORD rollback_remaining(ULONGLONG deadline){ULONGLONG now=GetTickCount64();return now<deadline?(DWORD)(deadline-now):0;}
static HANDLE fixed_lock(const wchar_t* name,ULONGLONG deadline){
    HANDLE file=INVALID_HANDLE_VALUE;
    while(rollback_remaining(deadline)){
        file=CreateFileW(name,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,0,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        if(file!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_SHARING_VIOLATION)return INVALID_HANDLE_VALUE;
        DWORD left=rollback_remaining(deadline);if(left)Sleep(left<10?left:10);
    }
    if(file==INVALID_HANDLE_VALUE){SetLastError(ERROR_TIMEOUT);return file;}
    BYTE* sd=NULL;DWORD size,sd_size;bool ok=regular(file,name,0,&size) && l4_store_security(file,true,&sd,&sd_size) && rollback_remaining(deadline);
    free(sd);if(!ok){DWORD error=GetLastError();CloseHandle(file);SetLastError(error?error:ERROR_TIMEOUT);return INVALID_HANDLE_VALUE;}return file;
}
HANDLE rollback_lock(const L4Layout* roots,ULONGLONG deadline){
    wchar_t name[MAX_PATH];if(!l4_layout_data_path(roots,L"update\\operations\\deployment.lock",name))return INVALID_HANDLE_VALUE;
    return fixed_lock(name,deadline);
}
HANDLE rollback_runner(const L4Layout* roots,const wchar_t* operation,ULONGLONG deadline){
    wchar_t name[MAX_PATH];if(swprintf_s(name,MAX_PATH,L"%ls\\%ls\\supervisor.runner.lock",roots->operations,operation)<0){SetLastError(ERROR_FILENAME_EXCED_RANGE);return INVALID_HANDLE_VALUE;}
    return fixed_lock(name,deadline);
}
/* Return an immutable exact old image, with protected parents held throughout SCM work. */
bool rollback_image(const L4Layout* roots,const L4RecoveryPlan* plan,HANDLE* file,L4FileFence* fence,wchar_t image[MAX_PATH]){
    *file=INVALID_HANDLE_VALUE;memset(fence,0,sizeof(*fence));const wchar_t* end=wcschr(plan->before+1,L'"');
    if(!end || end-plan->before-1>=MAX_PATH)return fail(ERROR_INVALID_DATA);
    wcsncpy_s(image,MAX_PATH,plan->before+1,(size_t)(end-plan->before-1));wchar_t parent[MAX_PATH];wcscpy_s(parent,MAX_PATH,image);*wcsrchr(parent,L'\\')=0;
    if(!l4_store_pin(parent,roots->binaries,false,fence) || !tail_policy(fence,4))goto failure;
    *file=CreateFileW(image,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    BYTE* sd=NULL;DWORD size=0,sd_size=0;bool ok=*file!=INVALID_HANDLE_VALUE && plan->old_size<=MAXDWORD && regular(*file,image,MAXDWORD,&size) && size==plan->old_size && read_policy(*file,&sd,&sd_size);
    free(sd);if(ok)ok=image_hash(*file,size,plan->old_sha256);if(ok)return true;
failure: {DWORD error=GetLastError();if(*file!=INVALID_HANDLE_VALUE)CloseHandle(*file);*file=INVALID_HANDLE_VALUE;l4_store_unpin(fence);return fail(error?error:ERROR_INVALID_DATA);}
}
static bool config_matches(const wchar_t* name,const L4RecoveryPlan* plan,bool* old){
    *old=false;HANDLE h=CreateFileW(name,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(h==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND && !plan->old_exists){*old=true;return true;}return false;}
    BYTE* sd=NULL;DWORD sd_size=0,size=0,count=0;bool ok=regular(h,name,L4_RECOVERY_CONFIG_LIMIT,&size) && read_policy(h,&sd,&sd_size);
    BYTE* bytes=ok?malloc(size?size:1):NULL;if(ok && !bytes){ok=false;SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
    if(ok)ok=ReadFile(h,bytes,size,&count,NULL) && count==size && sd_size==plan->config_sd_size && !memcmp(sd,plan->config_sd,sd_size);
    if(ok){*old=plan->old_exists && size==plan->old_config_size && (!size || !memcmp(bytes,plan->old_config,size));
        ok=*old || (size==plan->new_config_size && (!size || !memcmp(bytes,plan->new_config,size)));}
    DWORD error=GetLastError();free(sd);free(bytes);CloseHandle(h);return ok?true:fail(error?error:ERROR_REVISION_MISMATCH);
}
bool rollback_config(const L4Layout* roots,const L4RecoveryPlan* plan){
    wchar_t name[MAX_PATH],temp[MAX_PATH];L4FileFence fence={0};
    if(swprintf_s(name,MAX_PATH,L"%ls\\l4superv.json",roots->config)<0 || !l4_store_pin(roots->config,roots->data,false,&fence))return false;
    bool old=false,ok=tail_policy(&fence,2) && config_matches(name,plan,&old),created=false;HANDLE h=INVALID_HANDLE_VALUE;
    if(ok && !old && !plan->old_exists)ok=DeleteFileW(name)!=0;
    if(ok && !old && plan->old_exists){
        GUID id;wchar_t uuid[40];ok=SUCCEEDED(CoCreateGuid(&id)) && StringFromGUID2(&id,uuid,40)==39 && swprintf_s(temp,MAX_PATH,L"%ls\\.%ls.rollback.tmp",roots->config,uuid)>0;
        union{DWORD align;BYTE bytes[4096];} sd;memcpy(sd.bytes,plan->config_sd,plan->config_sd_size);
        HANDLE previous=NULL;bool scoped=false;if(ok){scoped=l4_layout_owner_begin(&previous);ok=scoped;}
        if(ok){SECURITY_ATTRIBUTES sa={sizeof(sa),sd.bytes,FALSE};h=CreateFileW(temp,GENERIC_WRITE|WRITE_DAC|WRITE_OWNER|DELETE,0,&sa,CREATE_NEW,FILE_FLAG_WRITE_THROUGH,NULL);created=h!=INVALID_HANDLE_VALUE;ok=created;}
        if(ok)ok=l4_store_write(h,plan->old_config,plan->old_config_size) && FlushFileBuffers(h);
        PSID owner=NULL,group=NULL;PACL acl=NULL;BOOL present,defaulted;SECURITY_DESCRIPTOR_CONTROL flags=0;DWORD revision;
        if(ok)ok=GetSecurityDescriptorOwner(sd.bytes,&owner,&defaulted) && GetSecurityDescriptorGroup(sd.bytes,&group,&defaulted) &&
            GetSecurityDescriptorDacl(sd.bytes,&present,&acl,&defaulted) && present && GetSecurityDescriptorControl(sd.bytes,&flags,&revision);
        if(ok){DWORD error=SetSecurityInfo(h,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|
            ((flags&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION),owner,group,acl,NULL);ok=error==ERROR_SUCCESS;if(!ok)SetLastError(error);}
        DWORD error=GetLastError();if(!ok && created){FILE_DISPOSITION_INFO remove={TRUE};SetFileInformationByHandle(h,FileDispositionInfo,&remove,sizeof(remove));}
        if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
        if(scoped && !l4_layout_owner_end(previous)){TerminateProcess(GetCurrentProcess(),ERROR_CANNOT_IMPERSONATE);ExitProcess(ERROR_CANNOT_IMPERSONATE);}
        if(ok){ok=MoveFileExW(temp,name,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;error=GetLastError();if(!ok)DeleteFileW(temp);}if(!ok)SetLastError(error);
    }
    if(ok)ok=config_matches(name,plan,&old) && old;
    DWORD error=GetLastError();l4_store_unpin(&fence);return ok?true:fail(error?error:ERROR_REVISION_MISMATCH);
}
