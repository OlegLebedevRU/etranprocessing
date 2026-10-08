#include "journal_internal.h"
#include <aclapi.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_LIMIT 65536u
/* Wire: version/existed/path bytes/old bytes/new bytes/SD bytes, then UTF-8 path,
 * original bytes, candidate bytes and a self-relative Windows SD. No ABI structs. */
typedef struct{bool exists;BYTE* bytes;DWORD size;BYTE* sd;DWORD sd_size;} Snapshot;
static void dispose(Snapshot* s){free(s->bytes);free(s->sd);memset(s,0,sizeof(*s));}
static bool no_streams(const wchar_t* path){
    WIN32_FIND_STREAM_DATA stream;HANDLE find=FindFirstStreamW(path,FindStreamInfoStandard,&stream,0);
    if(find==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_HANDLE_EOF;
    bool ok=true;do{if(wcscmp(stream.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(find,&stream));
    DWORD code=GetLastError();FindClose(find);return ok && code==ERROR_HANDLE_EOF?true:l4_store_fail(ERROR_INVALID_DATA);
}
static bool valid_sd(const BYTE* sd,DWORD size){
    if(size<sizeof(SECURITY_DESCRIPTOR_RELATIVE))return false;
    SECURITY_DESCRIPTOR_RELATIVE header;memcpy(&header,sd,sizeof(header));
    if(header.Revision!=SECURITY_DESCRIPTOR_REVISION || !(header.Control&SE_SELF_RELATIVE) ||
        !(header.Control&SE_DACL_PRESENT) || header.Sacl || !header.Owner || !header.Dacl)return false;
    DWORD sid_offsets[]={header.Owner,header.Group};
    for(unsigned i=0;i<2;i++){DWORD at=sid_offsets[i];if(!at && i==1)continue;
        if((at&3) || at<sizeof(header) || at>size || size-at<8 || sd[at]!=SID_REVISION || sd[at+1]>SID_MAX_SUB_AUTHORITIES ||
            8u+4u*sd[at+1]>size-at || !IsValidSid((PSID)(sd+at)))return false;
    }
    DWORD at=header.Dacl;if((at&3) || at<sizeof(header) || at>size || size-at<sizeof(ACL))return false;
    ACL acl;memcpy(&acl,sd+at,sizeof(acl));if(acl.AclSize<sizeof(ACL) || acl.AclSize>size-at)return false;
    DWORD ace_at=sizeof(ACL);
    for(WORD i=0;i<acl.AceCount;i++){
        if(ace_at>acl.AclSize || acl.AclSize-ace_at<sizeof(ACCESS_ALLOWED_ACE))return false;
        ACCESS_ALLOWED_ACE ace;memcpy(&ace,sd+at+ace_at,sizeof(ace));
        if(ace.Header.AceType!=ACCESS_ALLOWED_ACE_TYPE || (ace.Header.AceSize&3) || ace.Header.AceSize<16 || ace.Header.AceSize>acl.AclSize-ace_at)return false;
        const BYTE* sid=sd+at+ace_at+8;
        if(sid[0]!=SID_REVISION || sid[1]>SID_MAX_SUB_AUTHORITIES || 16u+4u*sid[1]>ace.Header.AceSize || !IsValidSid((PSID)sid))return false;
        ace_at+=ace.Header.AceSize;
    }
    return IsValidSecurityDescriptor((PSECURITY_DESCRIPTOR)sd) && GetSecurityDescriptorLength((PSECURITY_DESCRIPTOR)sd)==size;
}
static bool read_file(const wchar_t* path,Snapshot* s){
    memset(s,0,sizeof(*s));HANDLE file=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(file==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return true;return false;}
    BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER size={0};
    bool ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 &&
        GetFileSizeEx(file,&size) && size.QuadPart>=0 && size.QuadPart<=CONFIG_LIMIT && no_streams(path) && l4_store_security(file,false,&s->sd,&s->sd_size);
    if(ok){s->size=(DWORD)size.QuadPart;s->bytes=(BYTE*)malloc(s->size?s->size:1);ok=s->bytes!=NULL;}
    DWORD read=0;if(ok)ok=ReadFile(file,s->bytes,s->size,&read,NULL) && read==s->size;
    DWORD code=GetLastError();CloseHandle(file);if(!ok){dispose(s);return l4_store_fail(code?code:ERROR_INVALID_DATA);}s->exists=true;return true;
}
/* Ask Windows for the inherited file policy, rather than copying directory ACEs. */
static bool file_policy(const wchar_t* parent,bool public_broker,BYTE** sd,DWORD* size){
    GUID guid;wchar_t id[48],temp[MAX_PATH];
    if(FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(&guid,id,_countof(id)) || wcslen(parent)+wcslen(id)+12>=MAX_PATH)return l4_store_fail(ERROR_FILENAME_EXCED_RANGE);
    swprintf_s(temp,MAX_PATH,L"%ls\\.l4acl-%ls",parent,id);
    HANDLE file=CreateFileW(temp,GENERIC_READ|READ_CONTROL|WRITE_DAC|DELETE,0,NULL,CREATE_NEW,FILE_FLAG_DELETE_ON_CLOSE|FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(file==INVALID_HANDLE_VALUE)return false;
    bool ok=true; if(public_broker){BYTE users[SECURITY_MAX_SID_SIZE];DWORD count=sizeof(users);PACL original=NULL,merged=NULL;PSECURITY_DESCRIPTOR descriptor=NULL;
        ok=CreateWellKnownSid(WinBuiltinUsersSid,NULL,users,&count)!=0;
        if(ok)ok=GetSecurityInfo(file,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,&original,NULL,&descriptor)==ERROR_SUCCESS;
        EXPLICIT_ACCESS_W access={0};access.grfAccessPermissions=FILE_GENERIC_READ;access.grfAccessMode=GRANT_ACCESS;BuildTrusteeWithSidW(&access.Trustee,users);
        if(ok)ok=SetEntriesInAclW(1,&access,original,&merged)==ERROR_SUCCESS;
        if(ok)ok=SetSecurityInfo(file,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,merged,NULL)==ERROR_SUCCESS;
        if(merged)LocalFree(merged);if(descriptor)LocalFree(descriptor);
    } if(ok)ok=l4_store_security(file,false,sd,size);DWORD code=GetLastError();CloseHandle(file);return ok?true:l4_store_fail(code);
}
static bool paths(L4Journal* j,const wchar_t* relative,wchar_t target[MAX_PATH],wchar_t parent[MAX_PATH],L4FileFence* fence){
    if(!j || !relative || !*relative || wcslen(relative)+8>=MAX_PATH)return l4_store_fail(ERROR_INVALID_PARAMETER);
    wchar_t leaf[MAX_PATH];swprintf_s(leaf,MAX_PATH,L"config\\%ls",relative);
    if(!l4_layout_data_path(&j->layout,leaf,target))return false;
    wcscpy_s(parent,MAX_PATH,target);*wcsrchr(parent,L'\\')=0;return l4_store_pin(parent,j->layout.data,false,fence);
}
static bool prepare(L4Journal* j,const wchar_t* relative,const void* bytes,DWORD size,ULONGLONG* sequence,bool public_broker){
    if(size>CONFIG_LIMIT || (size&&!bytes))return l4_store_fail(ERROR_INVALID_PARAMETER);
    wchar_t target[MAX_PATH],parent[MAX_PATH];L4FileFence fence;if(!paths(j,relative,target,parent,&fence))return false;
    Snapshot old;bool ok=read_file(target,&old);
    if(ok && public_broker && old.exists){ok=false;SetLastError(ERROR_ALREADY_EXISTS);}
    if(ok && !old.exists)ok=file_policy(parent,public_broker,&old.sd,&old.sd_size);
    int path_size=ok?WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,relative,-1,NULL,0,NULL,NULL):0;
    BYTE* record=NULL;DWORD length=0;
    if(ok && path_size>1){length=24+(DWORD)path_size-1+old.size+size+old.sd_size;record=(BYTE*)malloc(length);ok=record!=NULL;}
    else ok=false;
    if(ok){l4_store_u32(record,public_broker?2:1);l4_store_u32(record+4,old.exists?1:0);l4_store_u32(record+8,(DWORD)path_size-1);
        l4_store_u32(record+12,old.size);l4_store_u32(record+16,size);l4_store_u32(record+20,old.sd_size);
        ok=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,relative,-1,(char*)record+24,path_size,NULL,NULL)!=0;
        DWORD at=24+(DWORD)path_size-1;if(old.size)memcpy(record+at,old.bytes,old.size);at+=old.size;
        if(size)memcpy(record+at,bytes,size);at+=size;memcpy(record+at,old.sd,old.sd_size);
        if(ok)ok=l4_journal_append(j,L4_RECORD_CONFIG_PLAN,record,length,sequence);
    }
    DWORD code=GetLastError();free(record);dispose(&old);l4_store_unpin(&fence);return ok?true:l4_store_fail(code?code:ERROR_INVALID_DATA);
}
bool l4_config_prepare(L4Journal* j,const wchar_t* relative,const void* bytes,DWORD size,ULONGLONG* sequence){return prepare(j,relative,bytes,size,sequence,false);}
bool l4_config_prepare_public_broker(L4Journal* j,const void* bytes,DWORD size,ULONGLONG* sequence){return prepare(j,L"mosquitto\\mosquitto.conf",bytes,size,sequence,true);}
static bool same(const Snapshot* actual,bool exists,const BYTE* bytes,DWORD size,const BYTE* sd,DWORD sd_size){return actual->exists==exists && (!exists ||
    (actual->size==size && actual->sd_size==sd_size && (!size || !memcmp(actual->bytes,bytes,size)) && !memcmp(actual->sd,sd,sd_size)));}
static bool replace(const wchar_t* target,const wchar_t* parent,const BYTE* bytes,DWORD size,BYTE* sd){
    GUID guid;wchar_t id[48],temp[MAX_PATH];
    if(FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(&guid,id,_countof(id)) || wcslen(parent)+wcslen(id)+12>=MAX_PATH)return l4_store_fail(ERROR_FILENAME_EXCED_RANGE);
    swprintf_s(temp,MAX_PATH,L"%ls\\.l4cfg-%ls",parent,id);HANDLE previous=NULL;
    if(!l4_layout_owner_begin(&previous))return false;
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),sd,FALSE};
    HANDLE output=CreateFileW(temp,GENERIC_WRITE|WRITE_DAC|WRITE_OWNER,0,&attributes,CREATE_NEW,FILE_FLAG_WRITE_THROUGH,NULL);
    bool created=output!=INVALID_HANDLE_VALUE,ok=created;
    if(ok)ok=l4_store_write(output,bytes,size) && FlushFileBuffers(output);
    PSID owner=NULL,group=NULL;PACL acl=NULL;BOOL present,defaulted;SECURITY_DESCRIPTOR_CONTROL flags=0;DWORD revision;
    if(ok)ok=GetSecurityDescriptorOwner(sd,&owner,&defaulted) && GetSecurityDescriptorGroup(sd,&group,&defaulted) &&
        GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted) && present && GetSecurityDescriptorControl(sd,&flags,&revision);
    if(ok)ok=SetSecurityInfo(output,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|
        ((flags&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION),owner,group,acl,NULL)==ERROR_SUCCESS;
    DWORD code=GetLastError();if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
    bool restored=l4_layout_owner_end(previous);if(!restored){ok=false;code=GetLastError();}
    if(ok){ok=MoveFileExW(temp,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;if(!ok)code=GetLastError();}
    if(!ok){if(restored && created)DeleteFileW(temp);return l4_store_fail(code?code:ERROR_WRITE_FAULT);}return true;
}
static bool execute(L4Journal* j,ULONGLONG sequence,bool forward,bool verify_only){
    BYTE* record=NULL;DWORD length;if(!l4_store_find_record(j,L4_RECORD_CONFIG_PLAN,sequence,&record,&length))return false;
    if(length<24 || (l4_store_get32(record)!=1 && l4_store_get32(record)!=2)){free(record);return l4_store_fail(ERROR_INVALID_DATA);}
    DWORD existed=l4_store_get32(record+4),path_size=l4_store_get32(record+8),old_size=l4_store_get32(record+12),new_size=l4_store_get32(record+16),sd_size=l4_store_get32(record+20);
    bool ok=existed<=1 && path_size && path_size<MAX_PATH*4 && old_size<=CONFIG_LIMIT && new_size<=CONFIG_LIMIT && sd_size && sd_size<=4096 &&
        (ULONGLONG)24+path_size+old_size+new_size+sd_size==length && (existed || !old_size) && !memchr(record+24,0,path_size);
    wchar_t relative[MAX_PATH];int n=ok?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(char*)record+24,(int)path_size,relative,MAX_PATH-1):0;
    if(!n){free(record);return l4_store_fail(ERROR_INVALID_DATA);}relative[n]=0; bool public_broker=l4_store_get32(record)==2;
    if(public_broker && (existed || wcscmp(relative,L"mosquitto\\mosquitto.conf"))){free(record);return l4_store_fail(ERROR_INVALID_DATA);}
    BYTE* old=record+24+path_size;BYTE* candidate=old+old_size;
    /* Windows security APIs require aligned descriptors; wire offsets need not be. */
    BYTE* sd=(BYTE*)malloc(sd_size);if(!sd){free(record);return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);}memcpy(sd,candidate+new_size,sd_size);
    if(!valid_sd(sd,sd_size)){free(sd);free(record);return l4_store_fail(ERROR_INVALID_SECURITY_DESCR);}
    wchar_t target[MAX_PATH],parent[MAX_PATH];L4FileFence fence;if(!paths(j,relative,target,parent,&fence)){free(sd);free(record);return false;}
    Snapshot current;ok=read_file(target,&current);bool desired_exists=forward || existed;
    const BYTE* desired=forward?candidate:old;DWORD desired_size=forward?new_size:old_size;
    bool done=ok && same(&current,desired_exists,desired,desired_size,sd,sd_size);
    if(verify_only){
        if(ok && !done){ok=false;SetLastError(ERROR_RETRY);}
        if(ok && !current.exists){BYTE* policy=NULL;DWORD policy_size=0;
            ok=file_policy(parent,public_broker,&policy,&policy_size) && policy_size==sd_size && !memcmp(policy,sd,sd_size);free(policy);
            if(!ok && !GetLastError())SetLastError(ERROR_RETRY);
        }
        DWORD error=GetLastError();dispose(&current);l4_store_unpin(&fence);free(sd);free(record);
        return ok?true:l4_store_fail(error?error:ERROR_RETRY);
    }
    if(ok && !done && !same(&current,forward?existed:true,forward?old:candidate,forward?old_size:new_size,sd,sd_size)){ok=false;SetLastError(ERROR_RETRY);}
    /* For a fresh file, inherited file policy must still match the prepared policy. */
    if(ok && !current.exists){BYTE* parent_sd=NULL;DWORD parent_size=0;ok=file_policy(parent,public_broker,&parent_sd,&parent_size) && parent_size==sd_size && !memcmp(parent_sd,sd,sd_size);free(parent_sd);}
    BYTE intent[12];l4_store_u64(intent,sequence);l4_store_u32(intent+8,forward?1:0);
    if(ok)ok=l4_journal_append(j,L4_RECORD_CONFIG_INTENT,intent,sizeof(intent),NULL);
    if(ok && !done)ok=desired_exists?replace(target,parent,desired,desired_size,sd):DeleteFileW(target)!=0;
    DWORD code=GetLastError();dispose(&current);
    if(ok){Snapshot after;ok=read_file(target,&after) && same(&after,desired_exists,desired,desired_size,sd,sd_size);dispose(&after);if(!ok)SetLastError(ERROR_RETRY);}
    if(ok)ok=l4_journal_append(j,L4_RECORD_CONFIG_DONE,intent,sizeof(intent),NULL);
    if(!ok && GetLastError())code=GetLastError();free(sd);free(record);l4_store_unpin(&fence);return ok?true:l4_store_fail(code);
}
bool l4_config_apply(L4Journal* j,ULONGLONG sequence){return execute(j,sequence,true,false);}
bool l4_config_rollback(L4Journal* j,ULONGLONG sequence){return execute(j,sequence,false,false);}
bool l4_config_verify(L4Journal* j,ULONGLONG sequence,bool candidate){return execute(j,sequence,candidate,true);}
