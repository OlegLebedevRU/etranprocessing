#include "broker_config.h"
#include <string.h>
static bool reject(DWORD code){SetLastError(code);return false;}
static bool render(const L4Layout* layout,const char* sn,SetupBrokerConfig* out){
    if(!out)return reject(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));
    if(!layout)return reject(ERROR_INVALID_PARAMETER);
    const wchar_t* version=wcsrchr(layout->release,L'\\');L4Layout canonical;
    if(!version || !l4_layout_from_roots(&canonical,layout->binaries,layout->data,version+1) ||
       memcmp(layout,&canonical,sizeof(canonical)))return reject(ERROR_INVALID_DATA);
    wchar_t path[MAX_PATH];char log[MAX_PATH*3];
    if(!l4_layout_data_path(layout,L"logs\\mosquitto\\mosquitto.log",path) ||
       !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,log,sizeof(log),NULL,NULL))return false;
    return l4_broker_profile_render(log,1883,sn,out);
}
bool setup_broker_render(const L4Layout* layout,const char* sn,SetupBrokerConfig* out){
    if(!sn || !sn[0]){if(out)memset(out,0,sizeof(*out));return reject(ERROR_INVALID_PARAMETER);}
    return render(layout,sn,out);
}
bool setup_broker_render_standby(const L4Layout* layout,SetupBrokerConfig* out){
    return render(layout,NULL,out);
}
