#include "mosquitto_conf.h"
#include "service_mgr.h"
#include <stdio.h>
#include "../../l4common/layout.h"
#include "../../l4common/broker_profile.h"
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
#include <io.h>
#include <aclapi.h>
#pragma comment(lib, "advapi32.lib")

/* Built-in configuration contains only local routing, never credentials.
 * Custom templates and their replacements retain the private parent policy. */
static bool config_policy(const wchar_t* source,const wchar_t* target,bool public_read) {
    HANDLE input=CreateFileW(source,READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(input==INVALID_HANDLE_VALUE)return false;
    PSECURITY_DESCRIPTOR sd=NULL;PACL acl=NULL,merged=NULL;
    BY_HANDLE_FILE_INFORMATION source_info;
    bool ok=GetFileInformationByHandle(input,&source_info) && source_info.nNumberOfLinks==1 &&
        !(source_info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
    if(ok)ok=GetSecurityInfo(input,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,&acl,NULL,&sd)==ERROR_SUCCESS && acl;
    SECURITY_DESCRIPTOR_CONTROL flags=0;DWORD revision=0;
    if(ok)ok=GetSecurityDescriptorControl(sd,&flags,&revision)!=0;
    if(ok && public_read){BYTE users[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(users);EXPLICIT_ACCESS_W access={0};
        ok=CreateWellKnownSid(WinBuiltinUsersSid,NULL,users,&size)!=0;
        access.grfAccessPermissions=FILE_GENERIC_READ;access.grfAccessMode=GRANT_ACCESS;BuildTrusteeWithSidW(&access.Trustee,users);
        if(ok)ok=SetEntriesInAclW(1,&access,acl,&merged)==ERROR_SUCCESS;
    }
    HANDLE output=INVALID_HANDLE_VALUE;
    if(ok){output=CreateFileW(target,WRITE_DAC|READ_CONTROL|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);ok=output!=INVALID_HANDLE_VALUE;}
    if(ok){BY_HANDLE_FILE_INFORMATION info;ok=GetFileInformationByHandle(output,&info) && info.nNumberOfLinks==1 &&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
    if(ok)ok=SetSecurityInfo(output,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|
        ((flags&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION),
        NULL,NULL,merged?merged:acl,NULL)==ERROR_SUCCESS;
    DWORD error=GetLastError();if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);CloseHandle(input);
    if(merged)LocalFree(merged);if(sd)LocalFree(sd);if(!ok)SetLastError(error?error:ERROR_ACCESS_DENIED);return ok;
}

#pragma comment(lib, "shlwapi.lib")

/* Exact bridge routes shared by generation, migration and validation. */
static const char* const outbound_topics[]={"app","svc","evt","req","res","out","ctl","fmr"};
static const char* const inbound_topics[]={"tsk","rsp","eva","cmt","ctl","fmc"};
static int route_index(const char* topic,const char* sn,const char* direction) {
    char expected[256];
    for(unsigned i=0;i<_countof(outbound_topics);i++) {
        snprintf(expected,sizeof(expected),"dev/%s/%s",sn,outbound_topics[i]);
        if(!strcmp(topic,expected) && !strcmp(direction,"out"))return (int)i;
    }
    for(unsigned i=0;i<_countof(inbound_topics);i++) {
        snprintf(expected,sizeof(expected),"srv/%s/%s",sn,inbound_topics[i]);
        if(!strcmp(topic,expected) && !strcmp(direction,"in"))return (int)(_countof(outbound_topics)+i);
    }
    return -1;
}
static void write_routes(FILE* output,const char* sn) {
    for(unsigned i=0;i<_countof(outbound_topics);i++)fprintf(output,"topic dev/%s/%s out 1\n",sn,outbound_topics[i]);
    for(unsigned i=0;i<_countof(inbound_topics);i++)fprintf(output,"topic srv/%s/%s in 1\n",sn,inbound_topics[i]);
}

/* The installer and supervisor use this same route migration. Preserve the
 * terminal's non-routing settings, reject foreign routes, publish atomically. */
static bool migrate_config(const wchar_t* base_path,const wchar_t* source) {
    wchar_t target[MAX_PATH],candidate[MAX_PATH],backup[MAX_PATH];
    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf", L"mosquitto\\mosquitto.conf", target)) return false;
    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf.next", L"mosquitto\\mosquitto.conf.next", candidate)) return false;
    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf.previous", L"mosquitto\\mosquitto.conf.previous", backup)) return false;
    FILE* input=NULL;
    if(_wfopen_s(&input,source,L"rb") || !input)return false;
    char line[4096],sn[128]={0};bool ok=true,bridge=false,listener=false,address=false;
    unsigned bridges=0;size_t total=0;
    while(fgets(line,sizeof(line),input)) {
        total+=strlen(line);if(total>1048576 || (!strchr(line,'\n') && !feof(input))) {ok=false;break;}
        char* value=line;while(*value==' ' || *value=='\t')value++;
        if(*value=='#' || *value=='\n' || *value=='\r')continue;
        if(!strncmp(value,"include_dir ",12)) {ok=false;break;}
        if(!strncmp(value,"connection ",11)) {bridge=true;if(++bridges>1)ok=false;}
        if(!strncmp(value,"listener ",9)) {
            int port=0;char host[128]={0},extra[16]={0};
            if(sscanf_s(value,"listener %d %127s %15s",&port,host,(unsigned)sizeof(host),extra,(unsigned)sizeof(extra))!=2 ||
               port<1 || port>65535 || strcmp(host,"127.0.0.1"))ok=false;
            listener=true;
        }
        if(!strncmp(value,"address ",8)) {
            char endpoint[128]={0};sscanf_s(value,"address %127s",endpoint,(unsigned)sizeof(endpoint));
            address=!strcmp(endpoint,"127.0.0.1:18883");if(!address)ok=false;
        }
        if(!strncmp(value,"remote_clientid ",16)) {
            if(sscanf_s(value,"remote_clientid %127s",sn,(unsigned)sizeof(sn))!=1)ok=false;
            for(const char* p=sn;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')))ok=false;
        }
    }
    ok=ok && !ferror(input) && listener && (!bridge || (address && sn[0]));
    rewind(input);FILE* output=NULL;
    if(ok && (_wfopen_s(&output,candidate,L"wb") || !output))ok=false;
    bool routes=false;
    if(ok)fprintf(output,"# l4-topic-contract: 3\n");
    while(ok && fgets(line,sizeof(line),input)) {
        char* value=line;while(*value==' ' || *value=='\t')value++;
        if(!strncmp(value,"# l4-topic-contract:",20))continue;
        if(!strncmp(value,"topic ",6)) {
            char topic[256]={0},direction[16]={0},extra[32]={0};int qos=0;
            int fields=sscanf_s(value,"topic %255s %15s %d %31s",topic,(unsigned)sizeof(topic),direction,(unsigned)sizeof(direction),&qos,extra,(unsigned)sizeof(extra));
            char dev[160],srv[160];snprintf(dev,sizeof(dev),"dev/%s/",sn);snprintf(srv,sizeof(srv),"srv/%s/",sn);
            bool own_out=!strncmp(topic,dev,strlen(dev)) && !strcmp(direction,"out");
            bool own_in=!strncmp(topic,srv,strlen(srv)) && !strcmp(direction,"in");
            bool old_wildcard=(own_out && !strcmp(topic+strlen(dev),"#")) || (own_in && !strcmp(topic+strlen(srv),"#"));
            if(!bridge || fields!=3 || qos<0 || qos>1 || (!old_wildcard && route_index(topic,sn,direction)<0)) {ok=false;break;}
            if(!routes) {
                write_routes(output,sn);routes=true;
            }
            continue;
        }
        if(fputs(line,output)==EOF)ok=false;
    }
    if(ok && bridge && !routes)write_routes(output,sn);
    fclose(input);
    if(output) {ok=ok && !ferror(output) && fflush(output)==0 && _commit(_fileno(output))==0;if(fclose(output)!=0)ok=false;}
    if(ok)ok=config_policy(source,candidate,false);
    if(ok && GetFileAttributesW(target)!=INVALID_FILE_ATTRIBUTES)
        ok=CopyFileW(target,backup,FALSE)!=0 && config_policy(target,backup,false);
    if(ok)ok=MoveFileExW(candidate,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok)DeleteFileW(candidate);
    return ok;
}

bool mosquitto_conf_install_candidate(const wchar_t* base_path) {
    wchar_t source[MAX_PATH];if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf.candidate", L"mosquitto\\mosquitto.conf.candidate", source)) return false;
    bool ok=migrate_config(base_path,source);DeleteFileW(source);return ok;
}
bool mosquitto_conf_migrate(const wchar_t* base_path) {
    wchar_t source[MAX_PATH];if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf", L"mosquitto\\mosquitto.conf", source)) return false;
    return migrate_config(base_path,source);
}

static void get_forward_slash_path(const wchar_t* in_path, char* out_buf, size_t out_size) {
    char utf8[MAX_PATH * 3] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, in_path, -1, utf8, sizeof(utf8), NULL, NULL);
    for (size_t i = 0; utf8[i] && i + 1 < out_size; i++) {
        if (utf8[i] == '\\') {
            out_buf[i] = '/';
        } else {
            out_buf[i] = utf8[i];
        }
        out_buf[i + 1] = '\0';
    }
}

static bool ensure_log_dir_exists(const wchar_t* base_path) {
    wchar_t config[MAX_PATH], logs[MAX_PATH];
    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto", L"mosquitto", config) ||
        !l4_runtime_path(base_path, L4_DATA_LOGS, L"mosquitto", L"mosquitto\\log", logs)) return false;
    if (!CreateDirectoryW(config, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    if (!CreateDirectoryW(logs, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    return svc_configure_mosquitto_log(base_path);
}

static bool write_profile(const wchar_t* path,const char* log,int port,const char* sn){
    L4BrokerProfile profile;if(!l4_broker_profile_render(log,port>0?port:1883,sn,&profile))return false;
    FILE* file=NULL;if(_wfopen_s(&file,path,L"wb") || !file)return false;
    bool ok=fwrite(profile.bytes,1,profile.size,file)==profile.size && !ferror(file) &&
        fflush(file)==0 && _commit(_fileno(file))==0;
    if(fclose(file)!=0)ok=false;return ok;
}

bool mosquitto_conf_generate_standby(const wchar_t* base_path, int port) {
    if (!base_path) return false;
    if (!ensure_log_dir_exists(base_path)) return false;

    wchar_t conf_path[MAX_PATH];
    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf.candidate", L"mosquitto\\mosquitto.conf.candidate", conf_path)) return false;
    if(!DeleteFileW(conf_path) && GetLastError()!=ERROR_FILE_NOT_FOUND)return false;

    wchar_t log_path[MAX_PATH]; char log_fwd[MAX_PATH * 3] = {0};
    if (!l4_runtime_path(base_path, L4_DATA_LOGS, L"mosquitto\\mosquitto.log", L"mosquitto\\log\\mosquitto.log", log_path)) return false;
    get_forward_slash_path(log_path, log_fwd, sizeof(log_fwd));

    return write_profile(conf_path,log_fwd,port,NULL) &&
        config_policy(conf_path,conf_path,true) && mosquitto_conf_install_candidate(base_path);
}

bool mosquitto_conf_generate_active(const wchar_t* base_path,
                                    int port,
                                    const char* sn,
                                    const wchar_t* custom_tmpl_path) {
    if (!base_path || !sn || sn[0] == '\0') return false;
    if (!ensure_log_dir_exists(base_path)) return false;

    wchar_t conf_path[MAX_PATH];
    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf.candidate", L"mosquitto\\mosquitto.conf.candidate", conf_path)) return false;
    if(!DeleteFileW(conf_path) && GetLastError()!=ERROR_FILE_NOT_FOUND)return false;

    char base_fwd[MAX_PATH * 3] = { 0 };
    get_forward_slash_path(base_path, base_fwd, sizeof(base_fwd));
    wchar_t log_path[MAX_PATH]; char log_fwd[MAX_PATH * 3] = {0};
    if (!l4_runtime_path(base_path, L4_DATA_LOGS, L"mosquitto\\mosquitto.log", L"mosquitto\\log\\mosquitto.log", log_path)) return false;
    get_forward_slash_path(log_path, log_fwd, sizeof(log_fwd));

    // Check if custom template exists
    if (custom_tmpl_path && custom_tmpl_path[0] != L'\0' && PathFileExistsW(custom_tmpl_path)) {
        FILE* ft = NULL;
        if (_wfopen_s(&ft, custom_tmpl_path, L"rb") == 0 && ft) {
            fseek(ft, 0, SEEK_END);
            long sz = ftell(ft);
            fseek(ft, 0, SEEK_SET);

            if (sz > 0 && sz < 1024 * 1024) {
                char* tmpl_data = (char*)malloc(sz + 1);
                if (tmpl_data) {
                    size_t read_bytes = fread(tmpl_data, 1, sz, ft);
                    tmpl_data[read_bytes] = '\0';
                    fclose(ft);

                    /* The old macro means writable files beside EXEs. Refuse
                     * it for a versioned install; use explicit data macros. */
                    wchar_t config_dir[MAX_PATH], state_dir[MAX_PATH], portable_log[MAX_PATH];
                    char config_fwd[MAX_PATH * 3] = {0}, state_fwd[MAX_PATH * 3] = {0};
                    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto", L"mosquitto", config_dir) ||
                        !l4_runtime_path(base_path, L4_DATA_STATE, L"mosquitto", L"mosquitto", state_dir) ||
                        wcslen(base_path) + 30 >= MAX_PATH) { free(tmpl_data); return false; }
                    swprintf_s(portable_log, MAX_PATH, L"%ls\\mosquitto\\log\\mosquitto.log", base_path);
                    if (_wcsicmp(portable_log, log_path) && strstr(tmpl_data, "%BASE_PATH%")) {
                        free(tmpl_data); SetLastError(ERROR_INVALID_DATA); return false;
                    }
                    get_forward_slash_path(config_dir, config_fwd, sizeof(config_fwd));
                    get_forward_slash_path(state_dir, state_fwd, sizeof(state_fwd));

                    // Perform macro replacement (%SN%, %BASE_PATH%, %PORT%)
                    FILE* out_f = NULL;
                    if (_wfopen_s(&out_f, conf_path, L"wb") == 0 && out_f) {
                        for (size_t i = 0; i < read_bytes;) {
                            if (strncmp(tmpl_data + i, "%SN%", 4) == 0) {
                                fputs(sn, out_f);
                                i += 4;
                            } else if (strncmp(tmpl_data + i, "%BASE_PATH%", 11) == 0) {
                                fputs(base_fwd, out_f);
                                i += 11;
                            } else if (strncmp(tmpl_data + i, "%LOG_PATH%", 10) == 0) {
                                fputs(log_fwd, out_f);
                                i += 10;
                            } else if (strncmp(tmpl_data + i, "%CONFIG_DIR%", 12) == 0) {
                                fputs(config_fwd, out_f);
                                i += 12;
                            } else if (strncmp(tmpl_data + i, "%STATE_DIR%", 11) == 0) {
                                fputs(state_fwd, out_f);
                                i += 11;
                            } else if (strncmp(tmpl_data + i, "%PORT%", 6) == 0) {
                                fprintf(out_f, "%d", port > 0 ? port : 1883);
                                i += 6;
                            } else {
                                fputc(tmpl_data[i], out_f);
                                i++;
                            }
                        }
                        bool written=!ferror(out_f) && fflush(out_f)==0 && _commit(_fileno(out_f))==0;
                        if(fclose(out_f)!=0)written=false;
                        free(tmpl_data);
                        return written && mosquitto_conf_install_candidate(base_path);
                    }
                    free(tmpl_data);
                }
            } else {
                fclose(ft);
            }
        }
    }

    return write_profile(conf_path,log_fwd,port,sn) &&
        config_policy(conf_path,conf_path,true) && mosquitto_conf_install_candidate(base_path);
}

bool mosquitto_conf_is_standby(const wchar_t* base_path) {
    if (!base_path) return false;

    wchar_t conf_path[MAX_PATH];
    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf", L"mosquitto\\mosquitto.conf", conf_path)) return false;

    FILE* f = NULL;
    if (_wfopen_s(&f, conf_path, L"rb") != 0 || !f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) {
        fclose(f);
        return false;
    }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(buf, 1, sz, f);
    buf[read_bytes] = '\0';
    fclose(f);

    bool has_bridge = (strstr(buf, "connection platerra-upstream") != NULL);
    bool has_listener = (strstr(buf, "listener") != NULL);

    free(buf);
    return (!has_bridge && has_listener);
}

bool mosquitto_conf_is_active_with_sn(const wchar_t* base_path, const char* sn) {
    if (!base_path || !sn || sn[0] == '\0') return false;

    wchar_t conf_path[MAX_PATH];
    if (!l4_runtime_path(base_path, L4_DATA_CONFIG, L"mosquitto\\mosquitto.conf", L"mosquitto\\mosquitto.conf", conf_path)) return false;

    FILE* f = NULL;
    if (_wfopen_s(&f, conf_path, L"rb") != 0 || !f) {
        return false;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 1024 * 1024) {
        fclose(f);
        return false;
    }

    char* buf = (char*)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(buf, 1, sz, f);
    buf[read_bytes] = '\0';
    fclose(f);

    bool has_bridge=false,has_sn=false,current=false,valid=true;
    unsigned seen=0;
    char* context=NULL;
    for(char* line=strtok_s(buf,"\r\n",&context);line;line=strtok_s(NULL,"\r\n",&context)) {
        while(*line==' ' || *line=='\t')line++;
        if(!strcmp(line,"# l4-topic-contract: 3"))current=true;
        if(*line=='#')continue;
        if(!strcmp(line,"connection platerra-upstream"))has_bridge=true;
        if(!strncmp(line,"remote_clientid ",16))has_sn=!strcmp(line+16,sn);
        if(!strncmp(line,"topic ",6)) {
            char topic[256],direction[16],extra[16];int qos=0;
            int fields=sscanf_s(line,"topic %255s %15s %d %15s",topic,(unsigned)sizeof(topic),direction,(unsigned)sizeof(direction),&qos,extra,(unsigned)sizeof(extra));
            int index=fields==3?route_index(topic,sn,direction):-1;
            if(index<0 || qos!=1 || (seen & (1u<<index)))valid=false;
            else seen|=1u<<index;
        }
    }
    unsigned required=(1u<<(_countof(outbound_topics)+_countof(inbound_topics)))-1;
    free(buf);
    return has_bridge && has_sn && current && valid && seen==required;
}
