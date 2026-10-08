#include "launcher.h"
#include "journal_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const wchar_t* names[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv",L"l4desk",L"l4sql",L"l4pin",L"l4capture",L"ffmpeg"};
static const wchar_t* known(const wchar_t* tool){if(tool)for(unsigned i=0;i<_countof(names);i++)if(!_wcsicmp(tool,names[i]))return names[i];return NULL;}
static void executable_name(const wchar_t* name,wchar_t out[64]){
    if(!wcscmp(name,L"l4capture"))wcscpy_s(out,64,L"bin\\l4capture.exe");
    else swprintf_s(out,64,L"%ls.exe",name);
}
static bool relative(const wchar_t* tool,wchar_t out[MAX_PATH]){const wchar_t* name=known(tool);if(!name)return l4_store_fail(ERROR_INVALID_NAME);swprintf_s(out,MAX_PATH,L"launchers\\%ls.target",name);return true;}
static bool policy(HANDLE file,bool protected_required){
    BYTE* sd=NULL;DWORD size=0;if(!l4_store_security(file,false,&sd,&size))return false;
    BOOL present,defaulted;PACL acl=NULL;SECURITY_DESCRIPTOR_CONTROL flags=0;DWORD revision;
    bool ok=GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted) && present && acl && GetSecurityDescriptorControl(sd,&flags,&revision) && (!protected_required || (flags&SE_DACL_PROTECTED));
    for(WORD i=0;ok && i<acl->AceCount;i++){
        ACCESS_ALLOWED_ACE* ace=NULL;ok=GetAce(acl,i,(void**)&ace) && ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE;if(!ok)break;
        PSID sid=&ace->SidStart;
        if(IsWellKnownSid(sid,WinLocalSystemSid) || IsWellKnownSid(sid,WinBuiltinAdministratorsSid))continue;
        ok=IsWellKnownSid(sid,WinBuiltinUsersSid) && !(ace->Mask&~(FILE_GENERIC_READ|FILE_GENERIC_EXECUTE|GENERIC_READ|GENERIC_EXECUTE));
    }
    free(sd);return ok?true:l4_store_fail(ERROR_ACCESS_DENIED);
}
bool l4_launcher_prepare(L4Journal* j,const L4Layout* target,const wchar_t* tool,const L4ReleaseFile* files,unsigned count,ULONGLONG* sequence){
    if(!j || !target || _wcsicmp(j->layout.binaries,target->binaries) || _wcsicmp(j->layout.data,target->data))return l4_store_fail(ERROR_INVALID_PARAMETER);
    const wchar_t* name=known(tool);wchar_t leaf[MAX_PATH],exe[64];if(!name || !relative(name,leaf))return l4_store_fail(ERROR_INVALID_NAME);
    if(!l4_release_verify(target,files,count))return false;executable_name(name,exe);
    const L4ReleaseFile* selected=NULL;
    for(unsigned i=0;i<count;i++)if(!_wcsicmp(files[i].component,name) && !_wcsicmp(files[i].file,exe)){selected=&files[i];break;}
    if(!selected || !selected->size)return l4_store_fail(ERROR_FILE_NOT_FOUND);
    const wchar_t* version=wcsrchr(target->release,L'\\');char ascii[32],hash[65],record[160];
    if(!version || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,version+1,-1,ascii,sizeof(ascii),NULL,NULL))return l4_store_fail(ERROR_INVALID_DATA);
    for(unsigned i=0;i<32;i++)sprintf_s(hash+i*2,3,"%02x",selected->sha256[i]);
    int n=sprintf_s(record,sizeof(record),"L4CLI1\n%s\n%llu\n%s\n",ascii,selected->size,hash);
    return n>0 && l4_config_prepare(j,leaf,record,(DWORD)n,sequence);
}
static bool read_pointer(const L4Layout* roots,const wchar_t* tool,L4Layout* target,L4ReleaseFile* file){
    wchar_t leaf[MAX_PATH],path[MAX_PATH],parent[MAX_PATH];if(!relative(tool,leaf))return false;
    wchar_t config_leaf[MAX_PATH];swprintf_s(config_leaf,MAX_PATH,L"config\\%ls",leaf);
    if(!l4_layout_data_path(roots,config_leaf,path))return false;
    wcscpy_s(parent,MAX_PATH,path);*wcsrchr(parent,L'\\')=0;L4FileFence parents;
    if(!l4_store_pin(parent,roots->data,false,&parents))return false;
    bool ok=true;
    unsigned root_index=0;for(const wchar_t* p=roots->data+3;*p;p++)if(*p==L'\\')++root_index;
    for(unsigned i=root_index;ok && i<parents.count;i++)ok=policy(parents.handles[i],true);
    HANDLE input=INVALID_HANDLE_VALUE;
    if(ok){input=CreateFileW(path,GENERIC_READ|READ_CONTROL,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=input!=INVALID_HANDLE_VALUE;}
    char bytes[160]={0};DWORD size=0;BY_HANDLE_FILE_INFORMATION info;LARGE_INTEGER length={0};
    if(ok)ok=GetFileInformationByHandle(input,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 &&
        GetFileSizeEx(input,&length) && length.QuadPart>0 && length.QuadPart<(LONGLONG)sizeof(bytes) && policy(input,false) && ReadFile(input,bytes,(DWORD)length.QuadPart,&size,NULL) && size==(DWORD)length.QuadPart && !memchr(bytes,0,size);
    WIN32_FIND_STREAM_DATA stream;HANDLE search=INVALID_HANDLE_VALUE;
    if(ok){search=FindFirstStreamW(path,FindStreamInfoStandard,&stream,0);ok=search!=INVALID_HANDLE_VALUE;if(ok){do{if(wcscmp(stream.cStreamName,L"::$DATA")){ok=false;break;}}while(FindNextStreamW(search,&stream));if(ok)ok=GetLastError()==ERROR_HANDLE_EOF;}}
    if(search!=INVALID_HANDLE_VALUE)FindClose(search);DWORD code=GetLastError();if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);l4_store_unpin(&parents);
    if(!ok)return l4_store_fail(code?code:ERROR_INVALID_DATA);
    /* Exact ASCII grammar, no arbitrary path, command, JSON or extension fields. */
    if(size<8 || memcmp(bytes,"L4CLI1\n",7) || bytes[size-1]!='\n')return l4_store_fail(ERROR_INVALID_DATA);
    char* version=bytes+7;char* split=strchr(version,'\n');if(!split || split-version>=32)return l4_store_fail(ERROR_INVALID_DATA);*split=0;
    wchar_t wide[32];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide,_countof(wide)) || !l4_layout_from_roots(target,roots->binaries,roots->data,wide))return false;
    char* number=split+1;split=strchr(number,'\n');if(!split || split==number || (*number=='0' && split-number>1))return l4_store_fail(ERROR_INVALID_DATA);
    ULONGLONG count=0;for(char* p=number;p<split;p++){if(*p<'0' || *p>'9' || count>512ULL*1024*1024)return l4_store_fail(ERROR_INVALID_DATA);count=count*10+(unsigned)(*p-'0');}
    if(!count || count>512ULL*1024*1024 || bytes+size-(split+1)!=65)return l4_store_fail(ERROR_INVALID_DATA);
    BYTE hash[32];for(unsigned i=0;i<64;i++){char c=split[1+i];unsigned digit=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):16;
        if(digit==16)return l4_store_fail(ERROR_INVALID_DATA);if(!(i&1))hash[i/2]=(BYTE)(digit<<4);else hash[i/2]|=(BYTE)digit;}
    memset(file,0,sizeof(*file));file->size=count;memcpy(file->sha256,hash,32);return true;
}
bool l4_launcher_resolve(const L4Layout* roots,const wchar_t* tool,L4ReleaseFence** fence,wchar_t executable[MAX_PATH]){
    if(!roots || !fence || !executable)return l4_store_fail(ERROR_INVALID_PARAMETER);*fence=NULL;
    const wchar_t* name=known(tool);if(!name)return l4_store_fail(ERROR_INVALID_NAME);
    L4Layout target;L4ReleaseFile file;wchar_t exe[64];if(!read_pointer(roots,name,&target,&file))return false;
    executable_name(name,exe);file.component=name;file.file=exe;return l4_release_pin(&target,&file,fence,executable);
}
bool l4_launcher_identity(const wchar_t* self,const L4Layout* roots,const wchar_t** tool){
    if(!self || !roots || !tool)return l4_store_fail(ERROR_INVALID_PARAMETER);*tool=NULL;
    for(unsigned i=0;i<_countof(names);i++){wchar_t expected[MAX_PATH];if(wcslen(roots->launchers)+wcslen(names[i])+6>=MAX_PATH)return l4_store_fail(ERROR_FILENAME_EXCED_RANGE);
        swprintf_s(expected,MAX_PATH,L"%ls\\%ls.exe",roots->launchers,names[i]);if(!_wcsicmp(expected,self)){*tool=names[i];return true;}}
    return l4_store_fail(ERROR_INVALID_NAME);
}
bool l4_launcher_command(const wchar_t* executable,const wchar_t* original,wchar_t** command){
    if(!executable || !original || !command || wcschr(executable,L'"'))return l4_store_fail(ERROR_INVALID_PARAMETER);*command=NULL;
    const wchar_t* tail=original;bool quoted=false;
    /* Windows treats argv[0] specially: quote toggles, backslash is literal. */
    while(*tail){if(*tail==L'"')quoted=!quoted;else if(!quoted && (*tail==L' ' || *tail==L'\t'))break;++tail;}
    if(quoted)return l4_store_fail(ERROR_INVALID_DATA);
    size_t n=wcslen(executable)+wcslen(tail)+3;if(n>=32767)return l4_store_fail(ERROR_FILENAME_EXCED_RANGE);
    wchar_t* value=(wchar_t*)malloc((n+1)*sizeof(wchar_t));if(!value)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);
    swprintf_s(value,n+1,L"\"%ls\"%ls",executable,tail);*command=value;return true;
}
static BOOL WINAPI console_control(DWORD signal){return signal==CTRL_C_EVENT || signal==CTRL_BREAK_EVENT;}
bool l4_launcher_run(const L4Layout* roots,const wchar_t* tool,const wchar_t* original,DWORD* exit_code){
    if(!exit_code)return l4_store_fail(ERROR_INVALID_PARAMETER);
    L4ReleaseFence* target=NULL;wchar_t executable[MAX_PATH],work[MAX_PATH];wchar_t* command=NULL;
    if(!l4_launcher_resolve(roots,tool,&target,executable))return false;
    L4FileFence working={0};bool ok=l4_layout_data_path(roots,L"state\\l4con\\work",work) && l4_store_pin(work,roots->data,false,&working) && l4_launcher_command(executable,original,&command);
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);PROCESS_INFORMATION process={0};
    startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    startup.dwFlags=STARTF_USESTDHANDLES;
    bool handler=false;if(ok){handler=SetConsoleCtrlHandler(console_control,TRUE)!=0;ok=handler;}
    if(ok)ok=CreateProcessW(executable,command,NULL,NULL,TRUE,0,NULL,work,&startup,&process)!=0;
    DWORD code=GetLastError();if(ok){CloseHandle(process.hThread);ok=WaitForSingleObject(process.hProcess,INFINITE)==WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess,exit_code);code=GetLastError();CloseHandle(process.hProcess);}
    if(handler)SetConsoleCtrlHandler(console_control,FALSE);free(command);l4_store_unpin(&working);l4_release_unpin(target);return ok?true:l4_store_fail(code);
}


bool l4_launcher_install(L4Journal* j,const L4Layout* target,const wchar_t* tool,const L4ReleaseFile* files,unsigned count){
    if(!j || !target || _wcsicmp(j->layout.binaries,target->binaries) || _wcsicmp(j->layout.data,target->data))return l4_store_fail(ERROR_INVALID_PARAMETER);
    const wchar_t* name=known(tool);if(!name)return l4_store_fail(ERROR_INVALID_NAME);
    if(!l4_release_verify(target,files,count))return false;
    const L4ReleaseFile* file=NULL;for(unsigned i=0;i<count;i++)if(!_wcsicmp(files[i].component,L"l4launch") && !_wcsicmp(files[i].file,L"l4launch.exe")){file=&files[i];break;}
    if(!file)return l4_store_fail(ERROR_FILE_NOT_FOUND);
    char record[160],ascii[32],hash[65],version[32];const wchar_t* v=wcsrchr(target->release,L'\\');
    if(!v || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,ascii,sizeof(ascii),NULL,NULL) ||
        !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,v+1,-1,version,sizeof(version),NULL,NULL))return l4_store_fail(ERROR_INVALID_DATA);
    for(unsigned i=0;i<32;i++)sprintf_s(hash+i*2,3,"%02x",file->sha256[i]);
    int length=sprintf_s(record,sizeof(record),"L4BIN1\n%s\n%s\n%llu\n%s\n",ascii,version,file->size,hash);
    wchar_t leaf[64];swprintf_s(leaf,_countof(leaf),L"%ls.exe",name);
    if(length<=0 || !l4_journal_append(j,L4_RECORD_LAUNCHER_INTENT,record,(DWORD)length,NULL))return false;
    return l4_release_copy_launcher(target,file,leaf) && l4_journal_append(j,L4_RECORD_LAUNCHER_DONE,record,(DWORD)length,NULL);
}
