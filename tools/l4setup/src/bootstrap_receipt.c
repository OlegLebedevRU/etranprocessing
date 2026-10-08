#include "bootstrap_receipt.h"
#include "../../leo4proxy/src/policy_json.h"
#include <string.h>
#include <stdio.h>
static bool fail(DWORD e){SetLastError(e);return false;}
static bool fields(const PolicyJson* j,int n,unsigned expected){if(n<0 || j->tokens[n].type!='{')return false;unsigned count=0;for(int i=n+1;i<j->tokens[n].next;i=j->tokens[i+1].next)++count;return count==expected;}
static bool text(const PolicyJson* j,int n,const char* key,const char* expected){char value[128];return policy_json_string(j,policy_json_field(j,n,key),value,sizeof(value)) && !strcmp(value,expected);}
static bool digest(const PolicyJson* j,int n,const char* key,BYTE out[32]){char value[65];if(!policy_json_string(j,policy_json_field(j,n,key),value,sizeof(value)) || strlen(value)!=64)return false;BYTE any=0;for(unsigned i=0;i<32;i++){unsigned a=(unsigned char)value[2*i],b=(unsigned char)value[2*i+1];a=a>='0'&&a<='9'?a-'0':a>='a'&&a<='f'?a-'a'+10:16;b=b>='0'&&b<='9'?b-'0':b>='a'&&b<='f'?b-'a'+10:16;if(a>15 || b>15)return false;out[i]=(BYTE)((a<<4)|b);any|=out[i];}return any!=0;}
static bool parse(const void* bytes,DWORD size,const char* key,const L4CatalogRelease* binding,const char* arch,SetupBootstrapReceipt* out){
    if(!key || !arch || (strcmp(arch,"x86") && strcmp(arch,"x64")) || (binding && (binding->revoked || !memchr(binding->version,0,64))))return fail(ERROR_INVALID_PARAMETER);
    PolicyJson j;ULONGLONG schema=0;BYTE root[32];char version[64];wchar_t wide[64];L4Layout checked;
    if(!policy_json_parse(&j,bytes,size) || !fields(&j,0,7) || !policy_json_uint(&j,policy_json_field(&j,0,"schema"),&schema) || schema!=1 ||
       !text(&j,0,"kind","frozen-supervisor-helper") || !text(&j,0,"key_id",key) || !digest(&j,0,"manifest_sha256",root) ||
       !digest(&j,0,"publisher_certificate_sha256",out->publisher) || !policy_json_string(&j,policy_json_field(&j,0,"version"),version,64) ||
       !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide,64) || !l4_layout_from_roots(&checked,L"C:\\BootstrapFixture\\PF",L"C:\\BootstrapFixture\\PD",wide) ||
       (binding && (strcmp(version,binding->version) || memcmp(root,binding->manifest_sha256,32))))return fail(ERROR_INVALID_DATA);
    int helpers=policy_json_field(&j,0,"helpers");if(!fields(&j,helpers,2))return fail(ERROR_INVALID_DATA);
    const char* arches[]={"x86","x64"};for(unsigned i=0;i<2;i++){int item=policy_json_field(&j,helpers,arches[i]);char name[40];L4RecoveryHelper h={0};sprintf_s(name,sizeof(name),"l4rollback-%s.exe",arches[i]);
        if(!fields(&j,item,3) || !text(&j,item,"name",name) || !policy_json_uint(&j,policy_json_field(&j,item,"size"),&h.size) || !h.size || h.size>MAXDWORD || !digest(&j,item,"sha256",h.sha256))return fail(ERROR_INVALID_DATA);
        if(!strcmp(arch,arches[i]))out->helper=h;
    }return true;
}
static bool entry(const void* bytes,DWORD size,const BYTE* sig,DWORD sigsize,const BYTE* public_key,DWORD public_size,const char* key,const L4CatalogRelease* binding,const char* arch,SetupBootstrapReceipt* out,bool trusted){
    if(!out)return fail(ERROR_INVALID_PARAMETER);memset(out,0,sizeof(*out));BYTE hash[32];SetupBootstrapReceipt receipt={0};
    bool ok=trusted?l4_metadata_verify_trusted(bytes,size,sig,sigsize,hash):l4_metadata_verify(bytes,size,sig,sigsize,public_key,public_size,hash);
    if(ok)ok=parse(bytes,size,key,binding,arch,&receipt);if(ok){receipt.owner_trusted=trusted;*out=receipt;}return ok;
}
bool setup_bootstrap_receipt_trusted(const void* b,DWORD n,const BYTE* s,DWORD sn,const L4CatalogRelease* r,const char* a,SetupBootstrapReceipt* out){return entry(b,n,s,sn,NULL,0,l4_metadata_key_id(),r,a,out,true);}
bool setup_bootstrap_receipt_signed(const void* b,DWORD n,const BYTE* s,DWORD sn,const BYTE* p,DWORD pn,const char* k,const L4CatalogRelease* r,const char* a,SetupBootstrapReceipt* out){return entry(b,n,s,sn,p,pn,k,r,a,out,false);}
