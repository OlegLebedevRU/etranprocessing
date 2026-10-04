#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char* response;
bool http_get_simple(const char* url,int timeout,char** body,size_t* length) {
    (void)url;(void)timeout;
    if(!response) return false;
    *length=strlen(response);*body=_strdup(response);return *body!=NULL;
}
#define GOOD "{\"status\":\"ready\",\"certificate_found\":true,\"sn\":\"terminal-sn\",\"thumbprint\":\"0123456789012345678901234567890123456789\",\"serial\":\"serial\",\"not_after\":\"2027-10-02\"}"
int main(void) {
    ProxyIdentity identity;int failures=0;
    const char* bad_timeout[]={"0","-1","3601","text","120extra"};
    for (int i=0;i<5;i++) {AppConfig config;config_init_defaults(&config);
        char* argv[]={"l4con","--timeout",(char*)bad_timeout[i]};
        if (config_parse_args(&config,3,argv,NULL)) failures++;
    }
    AppConfig config;config_init_defaults(&config);
    char* valid[]={"l4con","--timeout","120"};
    if (!config_parse_args(&config,3,valid,NULL) || config.default_cmd_timeout!=120) failures++;

    response=GOOD;if(config_query_identity_from_proxy(18443,&identity)||strcmp(identity.sn,"terminal-sn"))failures++;
    const char* invalid[]={NULL,"{\"nested\":" GOOD "}",GOOD "extra",
        "{\"status\":\"ready\",\"status\":\"ready\",\"certificate_found\":true}",
        "{\"status\":\"waiting_for_certificate\",\"certificate_found\":false}",
        "{\"status\":\"ready\",\"certificate_found\":true", "{\"status\":\"ready\",\"certificate_found\":\"true\"}"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        response=invalid[i];memset(&identity,'x',sizeof(identity));
        if(!config_query_identity_from_proxy(18443,&identity)||identity.sn[0]) failures++;
    }
    char sn[64];response="terminal-sn\r\n";
    if(config_query_sn_from_proxy(18443,sn,sizeof(sn))||strcmp(sn,"terminal-sn"))failures++;
    const char* invalid_sn[]={"", "a/b", "two names", "{\"sn\":\"x\"}"};
    for(size_t i=0;i<4;i++){response=invalid_sn[i];if(!config_query_sn_from_proxy(18443,sn,sizeof(sn))||sn[0])failures++;}
    printf("Discovery/identity fixtures: failures=%d\n",failures);return failures?1:0;
}
