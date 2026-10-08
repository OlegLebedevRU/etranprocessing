#include "bootstrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(DWORD code){SetLastError(code);return false;}
bool l4_bootstrap_plan(const L4Layout* layout,const L4BootstrapProfile* profiles,unsigned profile_count,const L4ReleaseFile* files,unsigned count,L4BootstrapPlan* plan){
    if(!layout || !profiles || !plan || profile_count!=L4_BOOTSTRAP_SERVICES)return fail(ERROR_INVALID_PARAMETER);
    const wchar_t* services[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};
    L4BootstrapPlan result={0};result.layout=*layout;unsigned seen=0;
    if(!l4_release_verify(layout,files,count))return false;
    for(unsigned i=0;i<profile_count;i++){
        const L4BootstrapProfile* p=&profiles[i];unsigned at=L4_BOOTSTRAP_SERVICES;
        if(!p->service || !p->account || !p->arguments)return fail(ERROR_INVALID_PARAMETER);
        for(unsigned j=0;j<L4_BOOTSTRAP_SERVICES;j++)if(!_wcsicmp(p->service,services[j])){at=j;break;}
        if(at==L4_BOOTSTRAP_SERVICES || (seen&(1u<<at)))return fail(ERROR_INVALID_DATA);seen|=1u<<at;
        /* No silent account replacement, password handling or unsupported token profile. */
        if(_wcsicmp(p->account,L"LocalSystem"))return fail(ERROR_NOT_SUPPORTED);
        if(p->start_type!=SERVICE_DEMAND_START && p->start_type!=SERVICE_AUTO_START)return fail(ERROR_INVALID_DATA);
        if(wcslen(p->arguments)>1600)return fail(ERROR_FILENAME_EXCED_RANGE);
        for(const wchar_t* arg=p->arguments;*arg;arg++)if(*arg<32 && *arg!=L'\t')return fail(ERROR_INVALID_DATA);
        wchar_t exe[64],path[MAX_PATH];swprintf_s(exe,_countof(exe),L"%ls.exe",components[at]);
        const L4ReleaseFile* found=NULL;for(unsigned j=0;j<count;j++)if(!_wcsicmp(files[j].component,components[at]) && !_wcsicmp(files[j].file,exe)){found=&files[j];break;}
        if(!found || !found->size || !l4_layout_component(layout,components[at],exe,path))return fail(ERROR_FILE_NOT_FOUND);
        wcscpy_s(result.services[at],32,services[at]);swprintf_s(result.commands[at],2048,L"\"%ls\"%ls%ls",path,*p->arguments?L" ":L"",p->arguments);
        result.start_types[at]=p->start_type;result.sizes[at]=found->size;memcpy(result.sha256[at],found->sha256,32);
    }
    for(unsigned i=0;i<L4_BOOTSTRAP_SERVICES;i++){
        L4ServiceInventory current={0};if(!l4_service_inventory(services[i],&current))return false;
        if(current.installed)return fail(ERROR_SERVICE_EXISTS);
    }
    *plan=result;return true;
}
