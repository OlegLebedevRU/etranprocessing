#include "broker_environment.h"
#include "../../l4common/journal_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef L4_BROKER_SERVICE
#define L4_BROKER_SERVICE L"mosquitto"
#define L4_BROKER_VARIABLE L"MOSQUITTO_DIR"
#endif
static bool fail(DWORD code){SetLastError(code);return false;}
static bool expected(const L4BootstrapPlan* plan,wchar_t value[MAX_PATH+32],DWORD* size){wchar_t directory[MAX_PATH];
    if(!plan || wcscmp(plan->services[1],L4_BROKER_SERVICE) || !l4_layout_data_path(&plan->layout,L"config\\mosquitto",directory))return fail(ERROR_INVALID_DATA);
    memset(value,0,(MAX_PATH+32)*sizeof(wchar_t));int n=swprintf_s(value,MAX_PATH+32,L"%ls=%ls",L4_BROKER_VARIABLE,directory);if(n<0)return fail(ERROR_FILENAME_EXCED_RANGE);*size=(DWORD)((n+2)*sizeof(wchar_t));return true;
}
static bool fingerprint(SC_HANDLE service,const L4BootstrapPlan* plan,const wchar_t* owner,bool stopped){DWORD n=0;QueryServiceConfigW(service,NULL,0,&n);
    if(GetLastError()!=ERROR_INSUFFICIENT_BUFFER || n<sizeof(QUERY_SERVICE_CONFIGW) || n>65536)return fail(ERROR_INVALID_DATA);
    QUERY_SERVICE_CONFIGW* config=malloc(n);if(!config)return fail(ERROR_NOT_ENOUGH_MEMORY);bool ok=QueryServiceConfigW(service,config,n,&n)!=0;
    if(ok)ok=config->lpBinaryPathName && !wcscmp(config->lpBinaryPathName,plan->commands[1]) && config->lpServiceStartName && !_wcsicmp(config->lpServiceStartName,L"LocalSystem") &&
        config->dwServiceType==SERVICE_WIN32_OWN_PROCESS && (stopped?config->dwStartType==SERVICE_DEMAND_START:(config->dwStartType==SERVICE_DEMAND_START || config->dwStartType==plan->start_types[1])) &&
        (!owner || (config->lpDisplayName && !wcscmp(config->lpDisplayName,owner)));free(config);SERVICE_STATUS_PROCESS status={0};
    if(ok && stopped)ok=QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,(BYTE*)&status,sizeof(status),&n) && status.dwCurrentState==SERVICE_STOPPED && !status.dwProcessId;
    return ok?true:fail(ERROR_REVISION_MISMATCH);
}
static bool read_expected(HKEY key,const wchar_t* value,DWORD size,bool* missing){wchar_t actual[MAX_PATH+32];DWORD type=0,n=sizeof(actual);LONG e=RegQueryValueExW(key,L"Environment",NULL,&type,(BYTE*)actual,&n);
    *missing=e==ERROR_FILE_NOT_FOUND;if(*missing)return true;if(e)return fail((DWORD)e);return type==REG_MULTI_SZ && n==size && !memcmp(actual,value,size)?true:fail(ERROR_REVISION_MISMATCH);
}
bool setup_broker_environment_verify(const L4BootstrapPlan* plan){wchar_t value[MAX_PATH+32];DWORD size=0;HKEY key=NULL;
    if(!expected(plan,value,&size))return false;SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!scm)return false;
    SC_HANDLE service=OpenServiceW(scm,L4_BROKER_SERVICE,SERVICE_QUERY_CONFIG);bool ok=service && fingerprint(service,plan,NULL,false);LONG e=ERROR_SUCCESS;
    if(ok){e=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\" L4_BROKER_SERVICE,0,KEY_QUERY_VALUE|KEY_WOW64_64KEY,&key);ok=!e;}
    bool missing=true;if(ok)ok=read_expected(key,value,size,&missing) && !missing;DWORD code=e?(DWORD)e:GetLastError();if(key)RegCloseKey(key);if(service)CloseServiceHandle(service);CloseServiceHandle(scm);return ok?true:fail(code?code:ERROR_NOT_READY);
}
bool setup_broker_environment_prepare(L4Journal* j,ULONGLONG sequence){L4BootstrapPlan plan;wchar_t value[MAX_PATH+32],owner[128];DWORD size=0;
    if(!j || !j->lock || j->lock==INVALID_HANDLE_VALUE || j->poisoned || !l4_bootstrap_load(j,sequence,&plan) || !expected(&plan,value,&size))return false;
    const wchar_t* id=wcsrchr(j->directory,L'\\');if(!id || swprintf_s(owner,128,L"%ls [L4:%ls:%llu]",L4_BROKER_SERVICE,id+1,sequence)<0)return fail(ERROR_INVALID_DATA);
    SC_HANDLE scm=OpenSCManagerW(NULL,NULL,SC_MANAGER_CONNECT);if(!scm)return false;SC_HANDLE service=OpenServiceW(scm,L4_BROKER_SERVICE,SERVICE_QUERY_CONFIG|SERVICE_QUERY_STATUS);HKEY key=NULL;
    bool ok=service && fingerprint(service,&plan,owner,true);LONG e=ERROR_SUCCESS;
    if(ok){e=RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\" L4_BROKER_SERVICE,0,KEY_QUERY_VALUE|KEY_SET_VALUE|KEY_WOW64_64KEY,&key);ok=!e;}
    bool missing=true;if(ok)ok=read_expected(key,value,size,&missing);
    if(ok && missing){BYTE reference[8];l4_store_u64(reference,sequence);ok=l4_journal_append(j,86,reference,sizeof(reference),NULL) && fingerprint(service,&plan,owner,true) && read_expected(key,value,size,&missing) && missing;
        if(ok){e=RegSetValueExW(key,L"Environment",0,REG_MULTI_SZ,(BYTE*)value,size);ok=!e;if(ok){e=RegFlushKey(key);ok=!e;}}
        if(ok)ok=read_expected(key,value,size,&missing) && !missing && fingerprint(service,&plan,owner,true) && l4_journal_append(j,87,reference,sizeof(reference),NULL);
    }
    DWORD code=e?(DWORD)e:GetLastError();if(key)RegCloseKey(key);if(service)CloseServiceHandle(service);CloseServiceHandle(scm);return ok?true:fail(code?code:ERROR_REVISION_MISMATCH);
}
