#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/mosquitto_conf.h"
bool svc_configure_mosquitto_log(const wchar_t* base) {(void)base;return true;}
static wchar_t base[MAX_PATH],conf[MAX_PATH];
static void write_config(const char* routes) {
    FILE* f=NULL;assert(!_wfopen_s(&f,conf,L"wb") && f);
    fprintf(f,"listener 1883 127.0.0.1\nallow_anonymous true\nconnection platerra-upstream\naddress 127.0.0.1:18883\nremote_clientid fixture123\nkeepalive_interval 47\n%s",routes);
    assert(!fclose(f));
}
static void read_config(char* data,size_t cap) {
    FILE* f=NULL;assert(!_wfopen_s(&f,conf,L"rb") && f);size_t n=fread(data,1,cap-1,f);data[n]=0;assert(!ferror(f));fclose(f);
}
int main(void) {
    wchar_t temp[MAX_PATH],dir[MAX_PATH];assert(GetTempPathW(MAX_PATH,temp));
    swprintf_s(base,MAX_PATH,L"%sl4-topic-test-%lu-%llu",temp,GetCurrentProcessId(),GetTickCount64());assert(CreateDirectoryW(base,NULL));
    swprintf_s(dir,MAX_PATH,L"%s\\mosquitto",base);assert(CreateDirectoryW(dir,NULL));
    swprintf_s(conf,MAX_PATH,L"%s\\mosquitto.conf",dir);
    write_config("topic dev/fixture123/# out 0\ntopic dev/fixture123/res out 1\ntopic srv/fixture123/# in 1\n");
    assert(!mosquitto_conf_is_active_with_sn(base,"fixture123"));
    assert(mosquitto_conf_migrate(base));assert(mosquitto_conf_is_active_with_sn(base,"fixture123"));
    assert(!mosquitto_conf_is_active_with_sn(base,"fixture12"));
    char first[8192],second[8192];read_config(first,sizeof(first));assert(!strstr(first,"/#"));assert(strstr(first,"keepalive_interval 47"));
    assert(strstr(first,"topic dev/fixture123/fmr out 1\n"));assert(strstr(first,"topic srv/fixture123/fmc in 1\n"));
    unsigned count=0;for(char* at=first;(at=strstr(at,"topic "));at+=6)count++;assert(count==14);
    assert(mosquitto_conf_migrate(base));read_config(second,sizeof(second));assert(!strcmp(first,second));
    write_config("topic dev/foreign/evt out 1\n");read_config(first,sizeof(first));assert(!mosquitto_conf_migrate(base));read_config(second,sizeof(second));assert(!strcmp(first,second));
    write_config("topic dev/fixture123/unknown out 1\n");assert(!mosquitto_conf_migrate(base));
    write_config("# l4-topic-contract: 3\n# topic dev/fixture123/fmr out 1\n");assert(!mosquitto_conf_is_active_with_sn(base,"fixture123"));
    assert(DeleteFileW(conf));swprintf_s(conf,MAX_PATH,L"%s\\mosquitto.conf.previous",dir);assert(DeleteFileW(conf));
    assert(RemoveDirectoryW(dir));assert(RemoveDirectoryW(base));
    puts("Mosquitto exact routes: old wildcard migration, all 14 routes, idempotence, foreign/unknown rejection passed");return 0;
}
