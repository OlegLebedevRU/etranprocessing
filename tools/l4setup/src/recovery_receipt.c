#include "recovery_receipt.h"
#include "../../l4common/journal_internal.h"
#include <sddl.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct SetupRecoveryReceipt {L4FileFence fence;HANDLE helper,document,signature;SetupBootstrapReceipt receipt;};
static bool fail(DWORD e){SetLastError(e);return false;}
static bool authority(L4Journal* j){
    if(!j || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE || !j->file || j->file==INVALID_HANDLE_VALUE)return fail(ERROR_INVALID_PARAMETER);
    HANDLE token=NULL;if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token)){CloseHandle(token);return fail(ERROR_ACCESS_DENIED);}if(GetLastError()!=ERROR_NO_TOKEN)return false;
    BYTE bytes[256];DWORD size=0;bool ok=OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token) && GetTokenInformation(token,TokenUser,bytes,sizeof(bytes),&size) && IsWellKnownSid(((TOKEN_USER*)bytes)->User.Sid,WinLocalSystemSid);if(token)CloseHandle(token);
    const wchar_t* version=wcsrchr(j->layout.release,L'\\');L4Layout actual;if(!ok || !version || !l4_layout_resolve(&actual,version+1) || memcmp(&actual,&j->layout,sizeof(actual)))return fail(ERROR_ACCESS_DENIED);return true;
}
static bool readonly(HANDLE file){BYTE* bytes=NULL;DWORD size=0;if(!l4_store_security(file,false,&bytes,&size))return false;PACL acl=NULL;BOOL present=FALSE,def=FALSE;bool ok=GetSecurityDescriptorDacl(bytes,&present,&acl,&def) && present && acl;
    for(WORD i=0;ok && i<acl->AceCount;i++){ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,i,(void**)&ace) && ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE;if(!ok)break;PSID sid=&ace->SidStart;if(IsWellKnownSid(sid,WinLocalSystemSid) || IsWellKnownSid(sid,WinBuiltinAdministratorsSid))continue;ok=!(ace->Mask&~(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE));}free(bytes);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool safe(HANDLE file){BY_HANDLE_FILE_INFORMATION info;FILE_STREAM_INFO streams[128];
    return GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) &&
        GetFileInformationByHandleEx(file,FileStreamInfo,streams,sizeof(streams)) && streams[0].NextEntryOffset==0 && streams[0].StreamNameLength==14 &&
        !memcmp(streams[0].StreamName,L"::$DATA",14) && readonly(file);}
static HANDLE open(const wchar_t* path){HANDLE h=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);if(h!=INVALID_HANDLE_VALUE && !safe(h)){DWORD e=GetLastError();CloseHandle(h);SetLastError(e?e:ERROR_INVALID_DATA);return INVALID_HANDLE_VALUE;}return h;}
static bool read(HANDLE h,BYTE** bytes,DWORD* size,DWORD maximum){LARGE_INTEGER n,zero={0};if(!GetFileSizeEx(h,&n) || n.QuadPart<=0 || n.QuadPart>maximum || !SetFilePointerEx(h,zero,NULL,FILE_BEGIN))return fail(ERROR_INVALID_DATA);*size=(DWORD)n.QuadPart;*bytes=malloc(*size);DWORD got=0;return *bytes && ReadFile(h,*bytes,*size,&got,NULL) && got==*size;}
static bool hash(HANDLE h,const L4RecoveryHelper* expected){LARGE_INTEGER n,zero={0};if(!safe(h) || !GetFileSizeEx(h,&n) || n.QuadPart<0 || (ULONGLONG)n.QuadPart!=expected->size || !SetFilePointerEx(h,zero,NULL,FILE_BEGIN))return fail(ERROR_INVALID_DATA);
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE digest=NULL;BYTE actual[32],buffer[32768];DWORD got=0;ULONGLONG total=0;bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0 && BCryptCreateHash(alg,&digest,NULL,0,NULL,0,0)==0;
    while(ok){ok=ReadFile(h,buffer,sizeof(buffer),&got,NULL)!=0;if(!ok || !got)break;total+=got;ok=total<=expected->size && BCryptHashData(digest,buffer,got,0)==0;}if(ok)ok=total==expected->size && BCryptFinishHash(digest,actual,32,0)==0 && !memcmp(actual,expected->sha256,32);if(digest)BCryptDestroyHash(digest);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok?true:fail(ERROR_CRC);
}
static bool paths(L4Journal* j,wchar_t directory[MAX_PATH],wchar_t helper[MAX_PATH],wchar_t doc[MAX_PATH],wchar_t sig[MAX_PATH]){return swprintf_s(directory,MAX_PATH,L"%ls\\recovery",j->layout.binaries)>0 && swprintf_s(helper,MAX_PATH,L"%ls\\l4rollback.exe",directory)>0 && swprintf_s(doc,MAX_PATH,L"%ls\\l4tools-bootstrap.json",directory)>0 && swprintf_s(sig,MAX_PATH,L"%ls\\l4tools-bootstrap.json.sig",directory)>0;}
void setup_recovery_receipt_free(SetupRecoveryReceipt* r){if(!r)return;HANDLE hs[]={r->helper,r->document,r->signature};for(unsigned i=0;i<3;i++)if(hs[i] && hs[i]!=INVALID_HANDLE_VALUE)CloseHandle(hs[i]);l4_store_unpin(&r->fence);free(r);}
const L4RecoveryHelper* setup_recovery_receipt_helper(const SetupRecoveryReceipt* r){return r && r->receipt.owner_trusted?&r->receipt.helper:NULL;}
static bool load(L4Journal* j,const char* arch,SetupRecoveryReceipt** out,SetupAdmissionPolicy policy){
    if(!out)return fail(ERROR_INVALID_PARAMETER);*out=NULL;if(!authority(j))return false;wchar_t directory[MAX_PATH],helper[MAX_PATH],doc[MAX_PATH],sig[MAX_PATH];if(!paths(j,directory,helper,doc,sig))return fail(ERROR_FILENAME_EXCED_RANGE);
    SetupRecoveryReceipt* r=calloc(1,sizeof(*r));if(!r)return fail(ERROR_NOT_ENOUGH_MEMORY);BYTE *bytes=NULL,*signature=NULL;DWORD n=0,sn=0;bool ok=l4_store_pin(directory,j->layout.binaries,false,&r->fence);
    for(unsigned i=0;ok && i<r->fence.count;i++){/* Only protected PF ancestry is authority; outer KnownFolder ancestors retain native policy. */wchar_t name[MAX_PATH];DWORD got=GetFinalPathNameByHandleW(r->fence.handles[i],name,MAX_PATH,FILE_NAME_NORMALIZED);size_t root=wcslen(j->layout.binaries);if(!got || got>=MAX_PATH){ok=false;break;}if(!wcsncmp(name,L"\\\\?\\",4) && !_wcsnicmp(name+4,j->layout.binaries,root) && (!name[4+root] || name[4+root]==L'\\'))ok=readonly(r->fence.handles[i]);}
    if(ok){r->document=open(doc);r->signature=open(sig);r->helper=open(helper);ok=r->document!=INVALID_HANDLE_VALUE && r->signature!=INVALID_HANDLE_VALUE && r->helper!=INVALID_HANDLE_VALUE;}
    if(ok)ok=read(r->document,&bytes,&n,L4_METADATA_MAX_BYTES) && read(r->signature,&signature,&sn,L4_METADATA_SIGNATURE_BYTES) && setup_bootstrap_receipt_trusted(bytes,n,signature,sn,NULL,arch,&r->receipt) && hash(r->helper,&r->receipt.helper) && setup_signed_executable_policy(r->helper,helper,r->receipt.publisher,policy);
    DWORD e=GetLastError();free(bytes);free(signature);if(!ok){setup_recovery_receipt_free(r);return fail(e?e:ERROR_INVALID_DATA);}*out=r;return true;
}
bool setup_recovery_receipt_load(L4Journal* j,const char* arch,SetupRecoveryReceipt** out){return load(j,arch,out,SETUP_ADMISSION_STRICT_REMOTE);}
static bool complete_paths(const wchar_t* helper,const wchar_t* document,const wchar_t* signature){const wchar_t* paths_[]={helper,document,signature};for(unsigned i=0;i<3;i++){DWORD attrs=GetFileAttributesW(paths_[i]);if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return fail(ERROR_NOT_READY);}return true;}
static bool same_identity(const SetupBootstrapReceipt* a,const SetupBootstrapReceipt* b){return !memcmp(&a->helper,&b->helper,sizeof(a->helper)) && !memcmp(a->publisher,b->publisher,32);}
static bool destination_state(L4Journal* j,L4FileFence* parent,bool* absent){
    *absent=false;if(!l4_store_pin(j->layout.binaries,j->layout.binaries,false,parent))return false;
    if(!parent->count || !readonly(parent->handles[parent->count-1]))return fail(ERROR_ACCESS_DENIED);
    wchar_t directory[MAX_PATH],helper[MAX_PATH],doc[MAX_PATH],sig[MAX_PATH];if(!paths(j,directory,helper,doc,sig))return fail(ERROR_FILENAME_EXCED_RANGE);
    DWORD attrs=GetFileAttributesW(directory);if(attrs==INVALID_FILE_ATTRIBUTES){DWORD e=GetLastError();if(e!=ERROR_FILE_NOT_FOUND && e!=ERROR_PATH_NOT_FOUND)return fail(e);*absent=true;return true;}
    if(!(attrs&FILE_ATTRIBUTE_DIRECTORY) || (attrs&FILE_ATTRIBUTE_REPARSE_POINT))return fail(ERROR_ACCESS_DENIED);
    L4FileFence existing={0};bool ok=l4_store_pin(directory,j->layout.binaries,false,&existing);if(ok)ok=existing.count && readonly(existing.handles[existing.count-1]);if(ok)ok=complete_paths(helper,doc,sig);DWORD e=GetLastError();l4_store_unpin(&existing);return ok?true:fail(e?e:ERROR_ACCESS_DENIED);
}
bool setup_recovery_receipt_check(L4Journal* j,const char* arch,const SetupBootstrapReceipt* expected,SetupAdmissionPolicy policy){
    if(!arch || (strcmp(arch,"x86") && strcmp(arch,"x64")) || (policy!=SETUP_ADMISSION_STRICT_REMOTE && policy!=SETUP_ADMISSION_LOCAL_OFFLINE))return fail(ERROR_INVALID_PARAMETER);
    if(!authority(j) || !expected || !expected->owner_trusted)return fail(ERROR_ACCESS_DENIED);L4FileFence parent={0};bool absent=false;bool ok=destination_state(j,&parent,&absent);
    SetupRecoveryReceipt* existing=NULL;if(ok && !absent){ok=load(j,arch,&existing,policy);if(ok && !same_identity(&existing->receipt,expected))ok=fail(ERROR_REVISION_MISMATCH);}DWORD e=GetLastError();setup_recovery_receipt_free(existing);l4_store_unpin(&parent);return ok?true:fail(e?e:ERROR_REVISION_MISMATCH);
}
static bool write_new(const wchar_t* path,HANDLE source,const void* bytes,DWORD size,PSECURITY_DESCRIPTOR sd){SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};HANDLE h=CreateFileW(path,GENERIC_READ|GENERIC_WRITE|READ_CONTROL,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,NULL);if(h==INVALID_HANDLE_VALUE)return false;bool ok=true;DWORD got=0,written=0;BYTE buffer[32768];LARGE_INTEGER zero={0};
    if(source && source!=INVALID_HANDLE_VALUE){ok=SetFilePointerEx(source,zero,NULL,FILE_BEGIN)!=0;ULONGLONG total=0;while(ok){ok=ReadFile(source,buffer,sizeof(buffer),&got,NULL)!=0;if(!ok || !got)break;total+=got;ok=total<=size && WriteFile(h,buffer,got,&written,NULL) && written==got;}if(ok)ok=total==size;}else ok=WriteFile(h,bytes,size,&written,NULL) && written==size;
    if(ok)ok=FlushFileBuffers(h) && safe(h);DWORD e=GetLastError();CloseHandle(h);return ok?true:fail(e?e:ERROR_WRITE_FAULT);
}
bool setup_recovery_receipt_install(L4Journal* j,const char* arch,const SetupBootstrapReceipt* expected,HANDLE helper,const void* document,DWORD n,const BYTE* signature,DWORD sn,SetupAdmissionPolicy policy){
    if(!authority(j) || !expected || !expected->owner_trusted || j->sequence)return fail(ERROR_ACCESS_DENIED);SetupBootstrapReceipt checked={0};if(!setup_bootstrap_receipt_trusted(document,n,signature,sn,NULL,arch,&checked) || memcmp(&checked.helper,&expected->helper,sizeof(checked.helper)) || memcmp(checked.publisher,expected->publisher,32))return fail(ERROR_INVALID_DATA);
    wchar_t directory[MAX_PATH],target[MAX_PATH],doc[MAX_PATH],sig[MAX_PATH];if(!paths(j,directory,target,doc,sig))return fail(ERROR_FILENAME_EXCED_RANGE);
    PSECURITY_DESCRIPTOR sd=NULL;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:SYG:SYD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&sd,NULL))return false;SECURITY_ATTRIBUTES sa={sizeof(sa),sd,FALSE};
    bool created=CreateDirectoryW(directory,&sa)!=0;DWORD e=GetLastError();bool ok=created || e==ERROR_ALREADY_EXISTS;L4FileFence fence={0};if(ok)ok=l4_store_pin(directory,j->layout.binaries,false,&fence);if(ok && fence.count)ok=readonly(fence.handles[fence.count-1]);
    if(ok && created){/* A partial first install is deliberately fail-closed; no overwrite/cleanup adoption. */
        ok=write_new(target,helper,NULL,(DWORD)checked.helper.size,sd) && write_new(doc,NULL,document,n,sd) && write_new(sig,NULL,signature,sn,sd);
    }
    if(ok && !created)ok=complete_paths(target,doc,sig);
    e=GetLastError();LocalFree(sd);l4_store_unpin(&fence);if(!ok)return fail(e?e:ERROR_INVALID_DATA);
    SetupRecoveryReceipt* installed=NULL;ok=load(j,arch,&installed,policy);if(ok)ok=!memcmp(&installed->receipt.helper,&checked.helper,sizeof(checked.helper)) && !memcmp(installed->receipt.publisher,checked.publisher,32);e=GetLastError();setup_recovery_receipt_free(installed);return ok?true:fail(e?e:ERROR_REVISION_MISMATCH);
}
