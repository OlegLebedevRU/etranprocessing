#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <aclapi.h>
#include <sddl.h>
#include "../src/mosquitto_conf.h"
bool svc_configure_mosquitto_log(const wchar_t* base) {(void)base;return true;}
static wchar_t base[MAX_PATH],conf[MAX_PATH];
static void public_read(const wchar_t* path,bool expected){
    PACL acl=NULL;PSECURITY_DESCRIPTOR sd=NULL;BYTE users[SECURITY_MAX_SID_SIZE];DWORD size=sizeof(users);TRUSTEE_W t={0};ACCESS_MASK rights=0;
    assert(GetNamedSecurityInfoW((LPWSTR)path,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION,NULL,NULL,&acl,NULL,&sd)==ERROR_SUCCESS);
    assert(CreateWellKnownSid(WinBuiltinUsersSid,NULL,users,&size));BuildTrusteeWithSidW(&t,users);
    assert(GetEffectiveRightsFromAclW(acl,&t,&rights)==ERROR_SUCCESS);
    assert(((rights&FILE_GENERIC_READ)==FILE_GENERIC_READ)==expected);
    assert(!(rights&(FILE_WRITE_DATA|FILE_APPEND_DATA|WRITE_DAC|WRITE_OWNER|DELETE)));LocalFree(sd);
}
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
    PSECURITY_DESCRIPTOR private_sd=NULL;PACL private_acl=NULL;BOOL present,defaulted;
    assert(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)",1,&private_sd,NULL));
    assert(GetSecurityDescriptorDacl(private_sd,&present,&private_acl,&defaulted));
    assert(SetNamedSecurityInfoW(dir,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,NULL,NULL,private_acl,NULL)==ERROR_SUCCESS);LocalFree(private_sd);
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
    assert(mosquitto_conf_generate_standby(base,1883));
    assert(mosquitto_conf_is_standby(base));read_config(first,sizeof(first));
    public_read(conf,true);public_read(dir,false);
    assert(strstr(first,"listener 1883 127.0.0.1\n"));assert(strstr(first,"/mosquitto/log/mosquitto.log\n"));
    wchar_t custom[MAX_PATH];swprintf_s(custom,MAX_PATH,L"%s\\custom.tmpl",base);
    FILE* fixture=NULL;assert(!_wfopen_s(&fixture,custom,L"wb") && fixture);
    fputs("listener %PORT% 127.0.0.1\nconnection platerra-upstream\naddress 127.0.0.1:18883\nremote_clientid %SN%\nlog_dest file %LOG_PATH%\n# config=%CONFIG_DIR% state=%STATE_DIR%\n",fixture);
    assert(!fclose(fixture));assert(mosquitto_conf_generate_active(base,1883,"fixture123",custom));
    assert(mosquitto_conf_is_active_with_sn(base,"fixture123"));read_config(first,sizeof(first));
    assert(strstr(first,"/mosquitto/log/mosquitto.log\n"));assert(!strstr(first,"%LOG_PATH%"));
    assert(!strstr(first,"%CONFIG_DIR%"));assert(!strstr(first,"%STATE_DIR%"));
    public_read(conf,false); /* Custom content never becomes public. */
    assert(mosquitto_conf_generate_active(base,1883,"fixture123",NULL));
    public_read(conf,true);wchar_t previous[MAX_PATH];swprintf_s(previous,MAX_PATH,L"%s\\mosquitto.conf.previous",dir);
    public_read(previous,false); /* Previous custom content retains privacy. */
    assert(DeleteFileW(custom));
    assert(DeleteFileW(conf));swprintf_s(conf,MAX_PATH,L"%s\\mosquitto.conf.previous",dir);assert(DeleteFileW(conf));
    swprintf_s(custom,MAX_PATH,L"%s\\log",dir);assert(RemoveDirectoryW(custom));
    assert(RemoveDirectoryW(dir));assert(RemoveDirectoryW(base));
    puts("Mosquitto exact routes: old wildcard migration, all 14 routes, idempotence, foreign/unknown rejection passed");return 0;
}
