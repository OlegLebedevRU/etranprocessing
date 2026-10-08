#include "service_switch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(DWORD code){SetLastError(code);return false;}
static bool component(const wchar_t* service,const wchar_t** name,const wchar_t** exe){
    if(!service)return false;
    const wchar_t* services[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    const wchar_t* names[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};
    for(unsigned i=0;i<4;i++)if(!_wcsicmp(service,services[i])){*name=names[i];*exe=exes[i];return true;}return false;
}
static bool old_path(const L4Layout* layout,const L4ServiceInventory* before,const wchar_t* name,const wchar_t* exe,const wchar_t** suffix,L4Layout* old_layout){
    if(!before || !before->installed || !before->account[0] || before->image_path[0]!=L'"')return fail(ERROR_INVALID_DATA);
    const wchar_t* end=wcschr(before->image_path+1,L'"');if(!end || (end[1] && end[1]!=L' ' && end[1]!=L'\t'))return fail(ERROR_INVALID_DATA);
    size_t size=(size_t)(end-before->image_path-1);if(size>=MAX_PATH)return fail(ERROR_FILENAME_EXCED_RANGE);
    wchar_t image[MAX_PATH],prefix[MAX_PATH];wcsncpy_s(image,MAX_PATH,before->image_path+1,size);
    if(wcslen(layout->binaries)+10>=MAX_PATH)return fail(ERROR_FILENAME_EXCED_RANGE);
    swprintf_s(prefix,MAX_PATH,L"%ls\\releases\\",layout->binaries);size_t n=wcslen(prefix);
    if(_wcsnicmp(image,prefix,n))return fail(ERROR_NOT_SUPPORTED);
    const wchar_t* separator=wcschr(image+n,L'\\');if(!separator)return fail(ERROR_INVALID_NAME);
    wchar_t version[32],expected[MAX_PATH];size=(size_t)(separator-image-n);if(size>=_countof(version))return fail(ERROR_INVALID_NAME);
    wcsncpy_s(version,_countof(version),image+n,size);L4Layout previous;
    if(!l4_layout_from_roots(&previous,layout->binaries,layout->data,version) ||
        !l4_layout_component(&previous,name,exe,expected) || _wcsicmp(expected,image))return fail(ERROR_INVALID_NAME);
    *suffix=end+1;if(old_layout)*old_layout=previous;return true;
}
bool l4_service_switch_plan(const L4Layout* layout,const wchar_t* service,const L4ServiceInventory* before,const wchar_t* arguments,const L4ReleaseFile* files,unsigned count,const L4ReleaseFile* before_files,unsigned before_count,L4ServiceSwitch* plan){
    const wchar_t *name=NULL,*exe=NULL,*suffix=NULL;
    if(!layout || !plan || !component(service,&name,&exe))return fail(ERROR_INVALID_PARAMETER);
    L4Layout previous;
    if(!l4_release_verify(layout,files,count) || !old_path(layout,before,name,exe,&suffix,&previous) ||
        !l4_release_verify(&previous,before_files,before_count))return false;
    const L4ReleaseFile* target=NULL;
    for(unsigned i=0;i<count;i++)if(!_wcsicmp(files[i].component,name) && !_wcsicmp(files[i].file,exe)){target=&files[i];break;}
    const L4ReleaseFile* prior=NULL;
    for(unsigned i=0;i<before_count;i++)if(!_wcsicmp(before_files[i].component,name) && !_wcsicmp(before_files[i].file,exe)){prior=&before_files[i];break;}
    if(!target || !prior)return fail(ERROR_FILE_NOT_FOUND);
    const wchar_t* args=arguments?arguments:suffix;
    for(const wchar_t* p=args;*p;p++)if(*p<32 && *p!=L'\t')return fail(ERROR_INVALID_DATA);
    wchar_t image[MAX_PATH];if(!l4_layout_component(layout,name,exe,image))return false;
    if(wcslen(image)+wcslen(args)+4>=_countof(plan->after))return fail(ERROR_FILENAME_EXCED_RANGE);
    L4ServiceSwitch result={0};result.layout=*layout;result.before=*before;
    wcscpy_s(result.service,_countof(result.service),service);
    swprintf_s(result.after,_countof(result.after),arguments?L"\"%ls\" %ls":L"\"%ls\"%ls",image,args);
    result.size=target->size;memcpy(result.sha256,target->sha256,32);
    result.before_size=prior->size;memcpy(result.before_sha256,prior->sha256,32);*plan=result;return true;
}
static bool snapshot(SC_HANDLE service,L4ServiceInventory* inventory,DWORD* state){
    DWORD bytes=0;QueryServiceConfigW(service,NULL,0,&bytes);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || !bytes || bytes>65536)return fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* config=(QUERY_SERVICE_CONFIGW*)malloc(bytes);if(!config)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=QueryServiceConfigW(service,config,bytes,&bytes)!=0;DWORD code=GetLastError();
    if(ok){if(wcslen(config->lpBinaryPathName)>=_countof(inventory->image_path) || wcslen(config->lpServiceStartName)>=_countof(inventory->account)){ok=false;code=ERROR_FILENAME_EXCED_RANGE;}
        else{memset(inventory,0,sizeof(*inventory));inventory->installed=true;inventory->start_type=config->dwStartType;
            wcscpy_s(inventory->image_path,_countof(inventory->image_path),config->lpBinaryPathName);wcscpy_s(inventory->account,_countof(inventory->account),config->lpServiceStartName);}}
    free(config);SERVICE_STATUS_PROCESS status={0};
    if(ok){ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(LPBYTE)&status,sizeof(status),&bytes)!=0;code=GetLastError();}
    if(ok)*state=status.dwCurrentState;
    return ok?true:fail(code);
}
static bool same(const L4ServiceInventory* current,const L4ServiceInventory* expected,const wchar_t* path){
    return current->installed && current->start_type==expected->start_type &&
        !_wcsicmp(current->account,expected->account) && !wcscmp(current->image_path,path);
}
static bool execute(const L4ServiceSwitch* plan,bool rollback){
    if(!plan)return fail(ERROR_INVALID_PARAMETER);
    const wchar_t *name=NULL,*exe=NULL,*suffix=NULL;
    L4Layout previous;
    if(!component(plan->service,&name,&exe) || !old_path(&plan->layout,&plan->before,name,exe,&suffix,&previous))return false;
    wchar_t expected[MAX_PATH],command[2048];
    if(!l4_layout_component(&plan->layout,name,exe,expected))return false;
    swprintf_s(command,_countof(command),L"\"%ls\"",expected);size_t n=wcslen(command);
    if(wcsncmp(command,plan->after,n) || (plan->after[n] && plan->after[n]!=L' ' && plan->after[n]!=L'\t'))return fail(ERROR_INVALID_DATA);
    /* Keep approved target bytes and ancestors pinned throughout apply. */
    L4ReleaseFence* fence=NULL;L4ReleaseFile file={name,exe,plan->size,{0}};memcpy(file.sha256,plan->sha256,32);
    if(rollback){file.size=plan->before_size;memcpy(file.sha256,plan->before_sha256,32);}
    if(!l4_release_pin(rollback?&previous:&plan->layout,&file,&fence,expected))return false;
    SC_HANDLE manager=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT),service=NULL;bool ok=manager!=NULL;DWORD code=GetLastError();
    if(ok){service=OpenServiceW(manager,plan->service,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS|SERVICE_CHANGE_CONFIG);ok=service!=NULL;code=GetLastError();}
    L4ServiceInventory current={0};DWORD state=0;
    if(ok){ok=snapshot(service,&current,&state);code=GetLastError();}
    if(ok && state!=SERVICE_STOPPED){ok=false;code=ERROR_SERVICE_ALREADY_RUNNING;}
    const wchar_t* from=rollback?plan->after:plan->before.image_path;
    const wchar_t* to=rollback?plan->before.image_path:plan->after;
    /* A retry which is already at the requested configuration is harmless. */
    bool done=ok && same(&current,&plan->before,to);
    if(ok && !done && !same(&current,&plan->before,from)){ok=false;code=ERROR_RETRY;}
    if(ok && !done){
        ok=ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,SERVICE_NO_CHANGE,to,NULL,NULL,NULL,NULL,NULL,NULL)!=0;code=GetLastError();
        if(ok){ok=snapshot(service,&current,&state);code=GetLastError();if(ok && (state!=SERVICE_STOPPED || !same(&current,&plan->before,to))){ok=false;code=ERROR_RETRY;}}
    }
    if(service)CloseServiceHandle(service);if(manager)CloseServiceHandle(manager);l4_release_unpin(fence);
    return ok?true:fail(code);
}
bool l4_service_switch_apply(const L4ServiceSwitch* plan){return execute(plan,false);}
bool l4_service_switch_rollback(const L4ServiceSwitch* plan){return execute(plan,true);}
