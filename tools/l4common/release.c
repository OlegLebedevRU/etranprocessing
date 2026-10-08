#include "release.h"
#include "../l4superv/src/miniz.h"
#include <bcrypt.h>
#include <aclapi.h>
#include <sddl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib,"bcrypt.lib")

struct L4ReleaseFence { HANDLE held[MAX_PATH/2+2]; unsigned count; HANDLE file; };
static bool fail(DWORD code){SetLastError(code);return false;}
static bool inside(const wchar_t* root,const wchar_t* path){size_t n=wcslen(root);return !_wcsnicmp(root,path,n) && (path[n]==0 || path[n]==L'\\');}
static bool layout_valid(const L4Layout* layout){
    if(!layout)return fail(ERROR_INVALID_PARAMETER);
    const wchar_t* version=wcsrchr(layout->release,L'\\');L4Layout check;
    return version && l4_layout_from_roots(&check,layout->binaries,layout->data,version+1) &&
        !memcmp(&check,layout,sizeof(check)) ? true : fail(ERROR_INVALID_PARAMETER);
}
static bool file_path(const L4Layout* layout,const wchar_t* root,const L4ReleaseFile* file,wchar_t out[MAX_PATH]){
    wchar_t checked[MAX_PATH];
    if(!file || !l4_layout_component(layout,file->component,file->file,checked))return false;
    const wchar_t* tail=checked+wcslen(layout->release);
    if(wcslen(root)+wcslen(tail)>=MAX_PATH)return fail(ERROR_FILENAME_EXCED_RANGE);
    swprintf_s(out,MAX_PATH,L"%ls%ls",root,tail);return true;
}
static bool inventory(const L4Layout* layout,const L4ReleaseFile* files,unsigned count){
    if(!layout_valid(layout) || !files || !count || count>4096)return fail(ERROR_INVALID_PARAMETER);
    for(unsigned i=0;i<count;i++){
        wchar_t a[MAX_PATH];if(files[i].size>512ULL*1024*1024 || !file_path(layout,layout->release,&files[i],a))return fail(ERROR_INVALID_DATA);
        for(unsigned j=0;j<i;j++){wchar_t b[MAX_PATH];if(!file_path(layout,layout->release,&files[j],b))return false;
            if(inside(a,b) || inside(b,a))return fail(ERROR_DUP_NAME);}
    }
    return true;
}
static bool safe_acl(HANDLE object,bool protected_required){
    PSID owner=NULL;PACL acl=NULL;PSECURITY_DESCRIPTOR sd=NULL;
    DWORD code=GetSecurityInfo(object,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,NULL,&acl,NULL,&sd);
    if(code)return fail(code);
    SECURITY_DESCRIPTOR_CONTROL control=0;DWORD revision;
    bool ok=owner && (IsWellKnownSid(owner,WinLocalSystemSid) || IsWellKnownSid(owner,WinBuiltinAdministratorsSid)) && acl && acl->AceCount &&
        GetSecurityDescriptorControl(sd,&control,&revision) && (!protected_required || (control&SE_DACL_PROTECTED));
    for(WORD i=0;ok && i<acl->AceCount;i++){
        ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,i,(void**)&ace)!=0 && ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE;
        if(!ok)break;
        if(IsWellKnownSid(&ace->SidStart,WinLocalSystemSid) || IsWellKnownSid(&ace->SidStart,WinBuiltinAdministratorsSid))continue;
        DWORD read_only=FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE;
        ok=IsWellKnownSid(&ace->SidStart,WinBuiltinUsersSid) && !(ace->Mask&~read_only);
    }
    LocalFree(sd);return ok?true:fail(ERROR_ACCESS_DENIED);
}
static bool no_named_streams(const wchar_t* path){
    WIN32_FIND_STREAM_DATA data;HANDLE search=FindFirstStreamW(path,FindStreamInfoStandard,&data,0);
    if(search==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_HANDLE_EOF;
    bool ok=true;do{if(wcscmp(data.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(search,&data));
    DWORD code=GetLastError();FindClose(search);
    return ok && code==ERROR_HANDLE_EOF?true:fail(ERROR_INVALID_DATA);
}
void l4_release_unpin(L4ReleaseFence* fence){if(!fence)return;while(fence->count)CloseHandle(fence->held[--fence->count]);free(fence);}
static bool pin(const wchar_t* path,const wchar_t* secure_from,bool directory,bool sealed,L4ReleaseFence** result){
    *result=NULL;if(!inside(secure_from,path) || wcslen(path)>=MAX_PATH)return fail(ERROR_INVALID_NAME);
    L4ReleaseFence* fence=(L4ReleaseFence*)calloc(1,sizeof(*fence));if(!fence)return fail(ERROR_NOT_ENOUGH_MEMORY);
    wchar_t prefix[MAX_PATH];wcscpy_s(prefix,MAX_PATH,path);bool ok=true;DWORD code=0;
    for(wchar_t* p=prefix+3;;p++)if(!*p || *p==L'\\'){
        wchar_t saved=*p;*p=0;bool dir=saved!=0 || directory;
        HANDLE object=CreateFileW(prefix,READ_CONTROL|FILE_READ_ATTRIBUTES|(dir?0:GENERIC_READ),
            dir?FILE_SHARE_READ|FILE_SHARE_WRITE:FILE_SHARE_READ,NULL,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|(dir?FILE_FLAG_BACKUP_SEMANTICS:0),NULL);
        BY_HANDLE_FILE_INFORMATION info;
        if(object==INVALID_HANDLE_VALUE){ok=false;code=GetLastError();}
        else{
            fence->held[fence->count++]=object;
            if(!GetFileInformationByHandle(object,&info)){ok=false;code=GetLastError();}
            else if((info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
                (((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=dir) || (!dir && info.nNumberOfLinks!=1)){ok=false;code=ERROR_ACCESS_DENIED;}
            else if(inside(secure_from,prefix) && !safe_acl(object,sealed)){ok=false;code=GetLastError();}
            else if(!dir && sealed && !no_named_streams(prefix)){ok=false;code=GetLastError();}
            if(!saved)fence->file=object;
        }
        *p=saved;if(!ok || !saved)break;
    }
    if(!ok){l4_release_unpin(fence);return fail(code);}*result=fence;return true;
}
static bool hash_file(HANDLE source,HANDLE destination,const BYTE expected[32],ULONGLONG size){
    LARGE_INTEGER actual,zero={0};
    if(!GetFileSizeEx(source,&actual) || actual.QuadPart<0 || (ULONGLONG)actual.QuadPart!=size)return fail(ERROR_INVALID_DATA);
    if(!SetFilePointerEx(source,zero,NULL,FILE_BEGIN))return false;
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE digest[32],buffer[65536];
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)>=0;
    ULONGLONG total=0;
    while(ok && total<size){DWORD read=0,written=0;DWORD want=(DWORD)((size-total)>sizeof(buffer)?sizeof(buffer):(size-total));
        ok=ReadFile(source,buffer,want,&read,NULL)!=0 && read==want;
        if(ok)ok=BCryptHashData(hash,buffer,read,0)>=0;
        if(ok && destination!=INVALID_HANDLE_VALUE)ok=WriteFile(destination,buffer,read,&written,NULL)!=0 && written==read;
        total+=read;
    }
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0 && !memcmp(digest,expected,32);
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    if(ok && destination!=INVALID_HANDLE_VALUE)ok=FlushFileBuffers(destination)!=0;
    return ok?true:fail(ERROR_CRC);
}
bool l4_release_pin(const L4Layout* layout,const L4ReleaseFile* file,L4ReleaseFence** fence,wchar_t path[MAX_PATH]){
    if(!fence || !path)return fail(ERROR_INVALID_PARAMETER);*fence=NULL;
    if(!inventory(layout,file,1) || !file_path(layout,layout->release,file,path) ||
        !pin(path,layout->binaries,false,true,fence))return false;
    if(hash_file((*fence)->file,INVALID_HANDLE_VALUE,file->sha256,file->size))return true;
    DWORD code=GetLastError();l4_release_unpin(*fence);*fence=NULL;return fail(code);
}
static bool expected_directory(const L4Layout* layout,const wchar_t* root,const wchar_t* directory,const L4ReleaseFile* files,unsigned count){
    for(unsigned i=0;i<count;i++){wchar_t path[MAX_PATH];if(!file_path(layout,root,&files[i],path))return false;if(inside(directory,path) && _wcsicmp(directory,path))return true;}return false;
}
static bool walk(const L4Layout* layout,const wchar_t* root,const wchar_t* directory,const L4ReleaseFile* files,unsigned count,unsigned* found){
    L4ReleaseFence* fence=NULL;if(!pin(directory,layout->binaries,true,true,&fence))return false;
    wchar_t pattern[MAX_PATH];if(wcslen(directory)+3>=MAX_PATH){l4_release_unpin(fence);return fail(ERROR_FILENAME_EXCED_RANGE);}
    swprintf_s(pattern,MAX_PATH,L"%ls\\*",directory);WIN32_FIND_DATAW data;
    HANDLE search=FindFirstFileW(pattern,&data);bool ok=search!=INVALID_HANDLE_VALUE;DWORD code=GetLastError();
    if(ok)do{
        if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;
        wchar_t path[MAX_PATH];if(wcslen(directory)+wcslen(data.cFileName)+1>=MAX_PATH){ok=false;code=ERROR_FILENAME_EXCED_RANGE;break;}
        swprintf_s(path,MAX_PATH,L"%ls\\%ls",directory,data.cFileName);
        if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){ok=false;code=ERROR_ACCESS_DENIED;break;}
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){ok=expected_directory(layout,root,path,files,count) && walk(layout,root,path,files,count,found);if(!ok){code=GetLastError();break;}}
        else{bool match=false;for(unsigned i=0;i<count;i++){wchar_t expected[MAX_PATH];if(!file_path(layout,root,&files[i],expected)){ok=false;break;}if(!_wcsicmp(path,expected)){match=true;break;}}
            if(!ok || !match){ok=false;code=ERROR_INVALID_DATA;break;}++*found;}
    }while(FindNextFileW(search,&data));
    if(ok && GetLastError()!=ERROR_NO_MORE_FILES){ok=false;code=GetLastError();}
    if(search!=INVALID_HANDLE_VALUE)FindClose(search);l4_release_unpin(fence);return ok?true:fail(code?code:ERROR_INVALID_DATA);
}
static bool verify_at(const L4Layout* layout,const wchar_t* root,const L4ReleaseFile* files,unsigned count,L4ReleaseAdmission admit,void* context){
    for(unsigned i=0;i<count;i++){wchar_t path[MAX_PATH];L4ReleaseFence* fence=NULL;
        if(!file_path(layout,root,&files[i],path) || !pin(path,layout->binaries,false,true,&fence))return false;
        bool ok=hash_file(fence->file,INVALID_HANDLE_VALUE,files[i].sha256,files[i].size);
        if(ok && admit){LARGE_INTEGER zero={0};ok=SetFilePointerEx(fence->file,zero,NULL,FILE_BEGIN)!=0 && admit(fence->file,path,context);}
        DWORD code=GetLastError();l4_release_unpin(fence);if(!ok)return fail(code);
    }
    unsigned found=0;return walk(layout,root,root,files,count,&found) && found==count;
}
bool l4_release_verify(const L4Layout* layout,const L4ReleaseFile* files,unsigned count){return inventory(layout,files,count) && verify_at(layout,layout->release,files,count,NULL,NULL);}
static bool create_parents(const wchar_t* path,size_t start,PSECURITY_DESCRIPTOR descriptor){
    wchar_t prefix[MAX_PATH];wcscpy_s(prefix,MAX_PATH,path);SECURITY_ATTRIBUTES attributes={sizeof(attributes),descriptor,FALSE};
    for(wchar_t* p=prefix+start;*p;p++)if(*p==L'\\'){*p=0;
        if(!CreateDirectoryW(prefix,&attributes) && GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        L4ReleaseFence* fence=NULL;if(!pin(prefix,prefix,true,true,&fence))return false;l4_release_unpin(fence);*p=L'\\';}
    return true;
}
static bool publish(const L4Layout* layout,const wchar_t* archive,const BYTE archive_sha256[32],const wchar_t* expanded,const L4ReleaseFile* files,unsigned count,L4ReleaseAdmission admit,void* context){
    if(!archive || !archive_sha256 || !expanded || !inventory(layout,files,count))return fail(ERROR_INVALID_PARAMETER);
    /* Reject noncanonical paths as well as cache/staging escapes before opening. */
    wchar_t checked[MAX_PATH];size_t data_length=wcslen(layout->data);
    if(!inside(layout->cache,archive) || !inside(layout->staging,expanded) ||
        !l4_layout_data_path(layout,archive+data_length+1,checked) || _wcsicmp(checked,archive) ||
        !l4_layout_data_path(layout,expanded+data_length+1,checked) || _wcsicmp(checked,expanded))return fail(ERROR_INVALID_NAME);
    L4ReleaseFence* archive_fence=NULL;
    if(!pin(archive,layout->cache,false,false,&archive_fence))return false;
    LARGE_INTEGER size;bool ok=GetFileSizeEx(archive_fence->file,&size)!=0 && size.QuadPart>=0 && size.QuadPart<=1024LL*1024*1024;
    if(ok)ok=hash_file(archive_fence->file,INVALID_HANDLE_VALUE,archive_sha256,(ULONGLONG)size.QuadPart);
    DWORD code=GetLastError();l4_release_unpin(archive_fence);if(!ok)return fail(code?code:ERROR_INVALID_DATA);
    DWORD attrs=GetFileAttributesW(layout->release);
    if(attrs!=INVALID_FILE_ATTRIBUTES)return verify_at(layout,layout->release,files,count,admit,context);
    if(GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)return false;
    wchar_t parent[MAX_PATH];wcscpy_s(parent,MAX_PATH,layout->release);*wcsrchr(parent,L'\\')=0;
    L4ReleaseFence* parent_fence=NULL;if(!pin(parent,layout->binaries,true,true,&parent_fence))return false;
    GUID guid;wchar_t id[48],stage[MAX_PATH];
    if(FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(&guid,id,_countof(id)) || wcslen(parent)+wcslen(id)+11>=MAX_PATH){l4_release_unpin(parent_fence);return fail(ERROR_GEN_FAILURE);}
    swprintf_s(stage,MAX_PATH,L"%ls\\.pending-%ls",parent,id);
    PSECURITY_DESCRIPTOR sd=NULL;
    ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&sd,NULL)!=0;
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),sd,FALSE};
    if(ok)ok=CreateDirectoryW(stage,&attributes)!=0;
    for(unsigned i=0;ok && i<count;i++){
        wchar_t source[MAX_PATH],target[MAX_PATH];L4ReleaseFence* source_fence=NULL;
        ok=file_path(layout,expanded,&files[i],source) && file_path(layout,stage,&files[i],target) &&
            create_parents(target,wcslen(stage)+1,sd) && pin(source,layout->staging,false,false,&source_fence);
        HANDLE output=INVALID_HANDLE_VALUE;
        if(ok){output=CreateFileW(target,GENERIC_WRITE|READ_CONTROL,0,&attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);ok=output!=INVALID_HANDLE_VALUE;}
        if(ok)ok=hash_file(source_fence->file,output,files[i].sha256,files[i].size) && safe_acl(output,true);
        code=GetLastError();if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);l4_release_unpin(source_fence);if(!ok)SetLastError(code);
    }
    if(ok)ok=verify_at(layout,stage,files,count,admit,context);
    if(ok)ok=MoveFileExW(stage,layout->release,MOVEFILE_WRITE_THROUGH)!=0;
    code=GetLastError();LocalFree(sd);l4_release_unpin(parent_fence);
    /* Failed preparation stays quarantined under .pending-GUID. Never publish
     * partial bytes or recursively delete a directory selected by payload input. */
    return ok?true:fail(code);
}

static bool hash_memory(const void* data,DWORD size,const BYTE expected[32]){
    BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;BYTE digest[32];
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,(PUCHAR)data,size,0)>=0;
    if(ok)ok=BCryptFinishHash(hash,digest,32,0)>=0 && !memcmp(digest,expected,32);
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok?true:fail(ERROR_CRC);
}
static bool unpack_publish(const L4Layout* layout,const wchar_t* archive,const BYTE archive_sha256[32],const L4ReleaseFile* files,unsigned count,L4ReleaseAdmission admit,void* context){
    if(!archive || !archive_sha256 || !inventory(layout,files,count))return fail(ERROR_INVALID_PARAMETER);
    wchar_t checked[MAX_PATH];
    if(!inside(layout->cache,archive) || !l4_layout_data_path(layout,archive+wcslen(layout->data)+1,checked) || _wcsicmp(archive,checked))return fail(ERROR_INVALID_NAME);
    L4ReleaseFence* archive_fence=NULL;if(!pin(archive,layout->cache,false,false,&archive_fence))return false;
    LARGE_INTEGER size;bool ok=GetFileSizeEx(archive_fence->file,&size)!=0 && size.QuadPart>=22 && size.QuadPart<=1024LL*1024*1024;
    if(ok)ok=hash_file(archive_fence->file,INVALID_HANDLE_VALUE,archive_sha256,(ULONGLONG)size.QuadPart);
    mz_zip_archive zip={0};bool initialized=false;
    if(ok){ok=mz_zip_reader_init_handle(&zip,archive_fence->file,0)!=0;initialized=ok;}
    if(ok && (mz_zip_reader_get_num_files(&zip)<count || mz_zip_reader_get_num_files(&zip)>8192)){ok=false;SetLastError(ERROR_INVALID_DATA);}
    L4ReleaseFence* staging_fence=NULL;
    if(ok)ok=pin(layout->staging,layout->data,true,true,&staging_fence);
    GUID guid;wchar_t id[48],expanded[MAX_PATH]={0};PSECURITY_DESCRIPTOR sd=NULL;
    if(ok){ok=SUCCEEDED(CoCreateGuid(&guid)) && StringFromGUID2(&guid,id,_countof(id)) && wcslen(layout->staging)+wcslen(id)+11<MAX_PATH;if(!ok)SetLastError(ERROR_GEN_FAILURE);}
    if(ok){swprintf_s(expanded,MAX_PATH,L"%ls\\.payload-%ls",layout->staging,id);
        ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&sd,NULL)!=0;}
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),sd,FALSE};
    if(ok)ok=CreateDirectoryW(expanded,&attributes)!=0;
    bool seen[4096]={0};unsigned extracted=0;
    for(unsigned i=0;ok && i<mz_zip_reader_get_num_files(&zip);i++){
        mz_zip_archive_file_stat stat;wchar_t relative[MAX_PATH],target[MAX_PATH];
        ok=mz_zip_reader_file_stat(&zip,i,&stat)!=0 && !stat.m_is_encrypted && stat.m_is_supported && stat.m_comp_size<=512ULL*1024*1024 &&
            MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,stat.m_filename,-1,relative,MAX_PATH)!=0;
        if(!ok){SetLastError(ERROR_INVALID_DATA);break;}
        for(wchar_t* p=relative;*p;p++)if(*p==L'/')*p=L'\\';
        if(stat.m_is_directory){size_t n=wcslen(relative);if(n && relative[n-1]==L'\\')relative[n-1]=0;
            if(wcslen(expanded)+wcslen(relative)+1>=MAX_PATH){ok=false;SetLastError(ERROR_FILENAME_EXCED_RANGE);break;}
            swprintf_s(target,MAX_PATH,L"%ls\\%ls",expanded,relative);
            ok=expected_directory(layout,expanded,target,files,count);if(!ok)SetLastError(ERROR_INVALID_DATA);continue;}
        unsigned match=count;
        for(unsigned j=0;j<count;j++){wchar_t approved[MAX_PATH];if(!file_path(layout,layout->release,&files[j],approved)){ok=false;break;}
            if(!_wcsicmp(approved+wcslen(layout->release)+1,relative)){match=j;break;}}
        if(!ok || match==count || seen[match] || stat.m_uncomp_size!=files[match].size){ok=false;SetLastError(ERROR_INVALID_DATA);break;}
        size_t actual=0;void* bytes=mz_zip_reader_extract_file_to_heap(&zip,i,&actual,0);
        ok=bytes && actual==files[match].size && hash_memory(bytes,(DWORD)actual,files[match].sha256) &&
            file_path(layout,expanded,&files[match],target) && create_parents(target,wcslen(expanded)+1,sd);
        HANDLE output=INVALID_HANDLE_VALUE;
        if(ok){output=CreateFileW(target,GENERIC_WRITE,0,&attributes,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);ok=output!=INVALID_HANDLE_VALUE;}
        if(ok){DWORD written=0;ok=WriteFile(output,bytes,(DWORD)actual,&written,NULL)!=0 && written==actual && FlushFileBuffers(output);}
        DWORD code=GetLastError();if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);free(bytes);
        if(ok){seen[match]=true;++extracted;}else SetLastError(code?code:ERROR_INVALID_DATA);
    }
    if(ok && extracted!=count){ok=false;SetLastError(ERROR_INVALID_DATA);}
    DWORD code=GetLastError();if(initialized)mz_zip_reader_end(&zip);LocalFree(sd);l4_release_unpin(archive_fence);
    if(!ok){l4_release_unpin(staging_fence);return fail(code?code:ERROR_INVALID_DATA);}
    ok=publish(layout,archive,archive_sha256,expanded,files,count,admit,context);code=GetLastError();l4_release_unpin(staging_fence);
    return ok?true:fail(code);
}


bool l4_release_copy_launcher(const L4Layout* layout,const L4ReleaseFile* file,const wchar_t* leaf){
    if(!layout || !file || !file->component || !file->file || !leaf || _wcsicmp(file->component,L"l4launch") || _wcsicmp(file->file,L"l4launch.exe") || !*leaf || wcschr(leaf,L'\\') || wcschr(leaf,L'/'))return fail(ERROR_INVALID_PARAMETER);
    wchar_t checked[MAX_PATH];if(!l4_layout_component(layout,L"l4launch",leaf,checked) || wcslen(layout->launchers)+wcslen(leaf)+2>=MAX_PATH)return fail(ERROR_INVALID_NAME);
    wchar_t target[MAX_PATH],source[MAX_PATH];swprintf_s(target,MAX_PATH,L"%ls\\%ls",layout->launchers,leaf);
    L4ReleaseFence *input=NULL,*parent=NULL,*existing=NULL;
    if(!l4_release_pin(layout,file,&input,source))return false;
    bool ok=pin(layout->launchers,layout->binaries,true,true,&parent);DWORD code=GetLastError();
    if(ok && GetFileAttributesW(target)!=INVALID_FILE_ATTRIBUTES){
        ok=pin(target,layout->binaries,false,true,&existing) && hash_file(existing->file,INVALID_HANDLE_VALUE,file->sha256,file->size);
        code=GetLastError();l4_release_unpin(existing);l4_release_unpin(parent);l4_release_unpin(input);return ok?true:fail(code);
    }
    if(ok && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND){ok=false;code=GetLastError();}
    GUID guid;wchar_t id[48],temp[MAX_PATH]={0};
    if(ok){ok=SUCCEEDED(CoCreateGuid(&guid)) && StringFromGUID2(&guid,id,_countof(id)) && wcslen(layout->launchers)+wcslen(id)+12<MAX_PATH;if(!ok)code=ERROR_FILENAME_EXCED_RANGE;}
    if(ok)swprintf_s(temp,MAX_PATH,L"%ls\\.l4bin-%ls",layout->launchers,id);
    PSECURITY_DESCRIPTOR sd=NULL;
    if(ok){ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)",SDDL_REVISION_1,&sd,NULL)!=0;code=GetLastError();}
    SECURITY_ATTRIBUTES attributes={sizeof(attributes),sd,FALSE};HANDLE output=INVALID_HANDLE_VALUE;bool created=false;
    if(ok){output=CreateFileW(temp,GENERIC_WRITE|READ_CONTROL,0,&attributes,CREATE_NEW,FILE_FLAG_WRITE_THROUGH,NULL);created=output!=INVALID_HANDLE_VALUE;ok=created;code=GetLastError();}
    if(ok){ok=hash_file(input->file,output,file->sha256,file->size) && safe_acl(output,true);code=GetLastError();}
    if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
    if(ok){ok=MoveFileExW(temp,target,MOVEFILE_WRITE_THROUGH)!=0;code=GetLastError();}
    if(!ok && created)DeleteFileW(temp);
    if(ok){ok=pin(target,layout->binaries,false,true,&existing) && hash_file(existing->file,INVALID_HANDLE_VALUE,file->sha256,file->size);code=GetLastError();}
    LocalFree(sd);l4_release_unpin(existing);l4_release_unpin(parent);l4_release_unpin(input);return ok?true:fail(code);
}


bool l4_release_publish(const L4Layout* layout,const wchar_t* archive,const BYTE sha[32],const wchar_t* expanded,const L4ReleaseFile* files,unsigned count){
    return publish(layout,archive,sha,expanded,files,count,NULL,NULL);
}
bool l4_release_unpack_publish(const L4Layout* layout,const wchar_t* archive,const BYTE sha[32],const L4ReleaseFile* files,unsigned count){
    return unpack_publish(layout,archive,sha,files,count,NULL,NULL);
}
bool l4_release_unpack_publish_checked(const L4Layout* layout,const wchar_t* archive,const BYTE sha[32],const L4ReleaseFile* files,unsigned count,L4ReleaseAdmission admit,void* context){
    if(!admit)return fail(ERROR_INVALID_PARAMETER);
    return unpack_publish(layout,archive,sha,files,count,admit,context);
}
bool l4_release_verify_checked(const L4Layout* layout,const L4ReleaseFile* files,unsigned count,L4ReleaseAdmission admit,void* context){
    if(!admit)return fail(ERROR_INVALID_PARAMETER);
    return inventory(layout,files,count) && verify_at(layout,layout->release,files,count,admit,context);
}
