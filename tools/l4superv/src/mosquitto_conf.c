#include "mosquitto_conf.h"
#include "service_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
#include <io.h>

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
    swprintf_s(target,MAX_PATH,L"%s\\mosquitto\\mosquitto.conf",base_path);
    swprintf_s(candidate,MAX_PATH,L"%s\\mosquitto\\mosquitto.conf.next",base_path);
    swprintf_s(backup,MAX_PATH,L"%s\\mosquitto\\mosquitto.conf.previous",base_path);
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
    if(ok && GetFileAttributesW(target)!=INVALID_FILE_ATTRIBUTES)ok=CopyFileW(target,backup,FALSE)!=0;
    if(ok)ok=MoveFileExW(candidate,target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok)DeleteFileW(candidate);
    return ok;
}

bool mosquitto_conf_install_candidate(const wchar_t* base_path) {
    wchar_t source[MAX_PATH];swprintf_s(source,MAX_PATH,L"%s\\mosquitto\\mosquitto.conf.candidate",base_path);
    bool ok=migrate_config(base_path,source);DeleteFileW(source);return ok;
}
bool mosquitto_conf_migrate(const wchar_t* base_path) {
    wchar_t source[MAX_PATH];swprintf_s(source,MAX_PATH,L"%s\\mosquitto\\mosquitto.conf",base_path);
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

static void ensure_log_dir_exists(const wchar_t* base_path) {
    wchar_t mosq_dir[MAX_PATH];
    swprintf_s(mosq_dir, MAX_PATH, L"%s\\mosquitto", base_path);
    CreateDirectoryW(mosq_dir, NULL);

    wchar_t log_dir[MAX_PATH];
    swprintf_s(log_dir, MAX_PATH, L"%s\\mosquitto\\log", base_path);
    CreateDirectoryW(log_dir, NULL);
    svc_configure_mosquitto_log(base_path);
}

bool mosquitto_conf_generate_standby(const wchar_t* base_path, int port) {
    if (!base_path) return false;
    ensure_log_dir_exists(base_path);

    wchar_t conf_path[MAX_PATH];
    swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf.candidate", base_path);

    char base_fwd[MAX_PATH * 3] = { 0 };
    get_forward_slash_path(base_path, base_fwd, sizeof(base_fwd));

    FILE* f = NULL;
    if (_wfopen_s(&f, conf_path, L"wb") != 0 || !f) {
        return false;
    }

    fprintf(f, "# ==============================================================================\n");
    fprintf(f, "# Mosquitto MQTT Broker Configuration (Standby / Neutral Mode - Local Only)\n");
    fprintf(f, "# Generated automatically by l4superv (Waiting for Leo4 Certificate)\n");
    fprintf(f, "# ==============================================================================\n\n");
    fprintf(f, "listener %d 127.0.0.1\n", port > 0 ? port : 1883);
    fprintf(f, "allow_anonymous true\n\n");
    fprintf(f, "persistence false\n");
    fprintf(f, "log_dest file %s/mosquitto/log/mosquitto.log\n", base_fwd);
    fprintf(f, "log_type error\n");
    fprintf(f, "log_type warning\n");
    fprintf(f, "log_type notice\n");
    fprintf(f, "log_type information\n");
    fprintf(f, "log_type subscribe\n");
    fprintf(f, "log_type unsubscribe\n");
    fprintf(f, "connection_messages true\n");

    bool written=!ferror(f) && fflush(f)==0 && _commit(_fileno(f))==0;
    if(fclose(f)!=0)written=false;
    return written && mosquitto_conf_install_candidate(base_path);
}

bool mosquitto_conf_generate_active(const wchar_t* base_path,
                                    int port,
                                    const char* sn,
                                    const wchar_t* custom_tmpl_path) {
    if (!base_path || !sn || sn[0] == '\0') return false;
    ensure_log_dir_exists(base_path);

    wchar_t conf_path[MAX_PATH];
    swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf.candidate", base_path);

    char base_fwd[MAX_PATH * 3] = { 0 };
    get_forward_slash_path(base_path, base_fwd, sizeof(base_fwd));

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

    // Default built-in active template
    FILE* f = NULL;
    if (_wfopen_s(&f, conf_path, L"wb") != 0 || !f) {
        return false;
    }

    fprintf(f, "# ==============================================================================\n");
    fprintf(f, "# Mosquitto MQTT Broker Configuration (Active Bridge Mode)\n");
    fprintf(f, "# Generated automatically by l4superv for Device SN: %s\n", sn);
    fprintf(f, "# ==============================================================================\n\n");
    fprintf(f, "# Local listener for internal terminal processes\n");
    fprintf(f, "listener %d 127.0.0.1\n", port > 0 ? port : 1883);
    fprintf(f, "allow_anonymous true\n\n");
    fprintf(f, "# Bridge configuration to leo4proxy (Native SChannel mTLS tunnel)\n");
    fprintf(f, "connection platerra-upstream\n");
    fprintf(f, "bridge_protocol_version mqttv50\n");
    fprintf(f, "address 127.0.0.1:18883\n\n");
    fprintf(f, "# Remote client identifier for external broker\n");
    fprintf(f, "remote_clientid %s\n\n", sn);
    fprintf(f, "# Disable Mosquitto $SYS status topics (required for external broker compatibility)\n");
    fprintf(f, "try_private false\n");
    fprintf(f, "notifications false\n\n");
    fprintf(f, "# Topic routing rules (topic <pattern> <direction> <QoS>)\n");
    write_routes(f,sn);
    fprintf(f, "# Connection reliability and keep-alive\n");
    fprintf(f, "cleansession true\n");
    fprintf(f, "restart_timeout 5 60\n");
    fprintf(f, "keepalive_interval 60\n\n");
    fprintf(f, "persistence false\n");
    fprintf(f, "log_dest file %s/mosquitto/log/mosquitto.log\n", base_fwd);
    fprintf(f, "log_type error\n");
    fprintf(f, "log_type warning\n");
    fprintf(f, "log_type notice\n");
    fprintf(f, "log_type information\n");
    fprintf(f, "log_type subscribe\n");
    fprintf(f, "log_type unsubscribe\n");
    fprintf(f, "connection_messages true\n");

    bool written=!ferror(f) && fflush(f)==0 && _commit(_fileno(f))==0;
    if(fclose(f)!=0)written=false;
    return written && mosquitto_conf_install_candidate(base_path);
}

bool mosquitto_conf_is_standby(const wchar_t* base_path) {
    if (!base_path) return false;

    wchar_t conf_path[MAX_PATH];
    swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf", base_path);

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
    swprintf_s(conf_path, MAX_PATH, L"%s\\mosquitto\\mosquitto.conf", base_path);

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
