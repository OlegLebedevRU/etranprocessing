#include "catalog_internal.h"
#include "../leo4proxy/src/policy_json.h"
#include <stdlib.h>
#include <string.h>
#define MAX_TIME 253402300799ULL
static bool fail(DWORD code){SetLastError(code);return false;}
static bool fields(const PolicyJson* json,int object,unsigned expected){
    if(object<0 || json->tokens[object].type!='{')return false;unsigned count=0;
    for(int i=object+1;i<json->tokens[object].next;i=json->tokens[i+1].next)++count;return count==expected;
}
static bool text(const PolicyJson* j,int object,const char* key,char* out,size_t size){return policy_json_string(j,policy_json_field(j,object,key),out,size);}
static bool number(const PolicyJson* j,int object,const char* key,ULONGLONG* out){return policy_json_uint(j,policy_json_field(j,object,key),out) && *out>0;}
static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static bool digest(const PolicyJson* j,int object,const char* key,BYTE out[32]){
    char value[65];if(!text(j,object,key,value,sizeof(value)) || strlen(value)!=64)return false;BYTE any=0;
    for(unsigned i=0;i<32;i++){int a=hex(value[2*i]),b=hex(value[2*i+1]);if(a<0||b<0)return false;out[i]=(BYTE)((a<<4)|b);any|=out[i];}return any!=0;
}
static bool version_valid(const char* version){wchar_t wide[64];L4Layout layout;const char* p=version;
    for(unsigned part=0;part<3;part++){unsigned value=0;while(*p>='0' && *p<='9'){value=value*10+(unsigned)(*p++-'0');if(value>65535)return false;}if(part<2 && *p++!='.')return false;}
    return MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide,64) && l4_layout_from_roots(&layout,L"C:\\L4Catalog\\Programs",L"C:\\L4Catalog\\Data",wide);}
static int find(const L4Catalog* c,const char* version){for(unsigned i=0;i<c->release_count;i++)if(!strcmp(version,c->releases[i].version))return (int)i;return -1;}
static bool profile_valid(const char* profile){size_t n=strlen(profile);if(!n || n>=64 || !((profile[0]>='a'&&profile[0]<='z') || (profile[0]>='0'&&profile[0]<='9')))return false;
    for(const char* p=profile;*p;p++)if(!((*p>='a'&&*p<='z') || (*p>='0'&&*p<='9') || strchr("_.-",*p)))return false;return true;}
void l4_catalog_free(L4Catalog* c){free(c);}
bool l4_catalog_resolve(const L4Catalog* c,const char* requested,L4CatalogRelease* release){
    if(!release)return fail(ERROR_INVALID_PARAMETER);memset(release,0,sizeof(*release));
    if(!c || !requested)return fail(ERROR_INVALID_PARAMETER);int at=!strcmp(requested,"latest")?c->stable:find(c,requested);
    if(at<0 || c->releases[at].revoked)return fail(ERROR_NOT_FOUND);*release=c->releases[at];return true;
}
static bool parse(const void* bytes,DWORD size,const BYTE authenticated[32],const char* key_id,ULONGLONG now,L4Catalog** result){
    PolicyJson j;ULONGLONG schema;char identity[65];BYTE identity_digest[32];
    if(!key_id || !now || now>MAX_TIME || !policy_json_parse(&j,(const char*)bytes,size) || !fields(&j,0,8) ||
       !number(&j,0,"schema",&schema) || schema!=1 || !text(&j,0,"key_id",identity,sizeof(identity)) ||
       !digest(&j,0,"key_id",identity_digest) || strcmp(identity,key_id))return fail(ERROR_INVALID_DATA);
    L4Catalog* c=(L4Catalog*)calloc(1,sizeof(*c));if(!c)return fail(ERROR_NOT_ENOUGH_MEMORY);c->stable=-1;
    memcpy(c->digest,authenticated,32);strcpy_s(c->key_id,sizeof(c->key_id),identity);
    bool ok=number(&j,0,"revision",&c->revision) && number(&j,0,"issued_at",&c->issued) && number(&j,0,"expires_at",&c->expires) &&
        c->expires<=MAX_TIME && c->issued<=now && now<c->expires;
    int releases=policy_json_field(&j,0,"releases"),edges=policy_json_field(&j,0,"transitions");
    ok=ok && releases>=0 && j.tokens[releases].type=='[' && edges>=0 && j.tokens[edges].type=='[';
    if(ok)for(int n=releases+1;n<j.tokens[releases].next && ok;n=j.tokens[n].next){
        if(c->release_count==L4_CATALOG_MAX_RELEASES){ok=false;break;}L4CatalogRelease* r=&c->releases[c->release_count];
        ok=fields(&j,n,3) && text(&j,n,"version",r->version,sizeof(r->version)) && version_valid(r->version) && find(c,r->version)<0 &&
            digest(&j,n,"manifest_sha256",r->manifest_sha256) && policy_json_bool(&j,policy_json_field(&j,n,"revoked"),&r->revoked);
        if(ok)++c->release_count;
    }
    int stable=policy_json_field(&j,0,"stable");
    if(ok){ok=c->release_count>0 && stable>=0;if(ok && j.tokens[stable].type!='n'){char version[64];ok=policy_json_string(&j,stable,version,sizeof(version));
        if(ok){c->stable=find(c,version);ok=c->stable>=0 && !c->releases[c->stable].revoked;}}}
    if(ok)for(int n=edges+1;n<j.tokens[edges].next && ok;n=j.tokens[n].next){
        if(c->transition_count==L4_CATALOG_MAX_TRANSITIONS){ok=false;break;}L4CatalogTransition* e=&c->transitions[c->transition_count];char from[64],to[64];
        ok=fields(&j,n,5) && text(&j,n,"from",from,sizeof(from)) && text(&j,n,"to",to,sizeof(to)) && text(&j,n,"arch",e->arch,sizeof(e->arch)) &&
            (!strcmp(e->arch,"x86") || !strcmp(e->arch,"x64")) && text(&j,n,"profile",e->profile,sizeof(e->profile)) && profile_valid(e->profile) && digest(&j,n,"evidence_sha256",e->evidence);
        int a=ok?find(c,from):-1,b=ok?find(c,to):-1;ok=ok && a>=0 && b>=0 && a!=b;
        if(ok){e->from=(unsigned)a;e->to=(unsigned)b;for(unsigned i=0;i<c->transition_count;i++){L4CatalogTransition* prior=&c->transitions[i];
            if(prior->from==e->from && prior->to==e->to && !strcmp(prior->arch,e->arch) && !strcmp(prior->profile,e->profile)){ok=false;break;}}}
        if(ok)++c->transition_count;
    }
    if(!ok){free(c);return fail(ERROR_INVALID_DATA);}*result=c;return true;
}
bool l4_catalog_parse_trusted(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,ULONGLONG now,L4Catalog** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;BYTE hash[32];
    return l4_metadata_verify_trusted(bytes,size,signature,signature_size,hash) && parse(bytes,size,hash,l4_metadata_key_id(),now,result);
}
bool l4_catalog_parse_signed(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,const BYTE* public_key,DWORD public_size,const char* expected_key_id,ULONGLONG now,L4Catalog** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;BYTE hash[32];
    return l4_metadata_verify(bytes,size,signature,signature_size,public_key,public_size,hash) && parse(bytes,size,hash,expected_key_id,now,result);
}
bool l4_catalog_route(const L4Catalog* c,const char* current,const BYTE current_manifest[32],const char* requested,const char* arch,const char* profile,L4CatalogRoute* route){
    if(!route)return fail(ERROR_INVALID_PARAMETER);memset(route,0,sizeof(*route));
    if(!c || !current || !current_manifest || !requested || !arch || !profile || (strcmp(arch,"x86") && strcmp(arch,"x64")) || !profile_valid(profile))return fail(ERROR_INVALID_PARAMETER);
    int start=find(c,current),target=!strcmp(requested,"latest")?c->stable:find(c,requested);
    if(start<0 || target<0 || c->releases[target].revoked || memcmp(c->releases[start].manifest_sha256,current_manifest,32))return fail(ERROR_NOT_FOUND);
    int parent[L4_CATALOG_MAX_RELEASES];unsigned queue[L4_CATALOG_MAX_RELEASES],head=0,tail=0;
    for(unsigned i=0;i<c->release_count;i++)parent[i]=-1;parent[start]=start;queue[tail++]=(unsigned)start;
    while(head<tail && parent[target]<0){unsigned at=queue[head++];for(unsigned i=0;i<c->transition_count;i++){const L4CatalogTransition* e=&c->transitions[i];
        if(e->from==at && !strcmp(e->arch,arch) && !strcmp(e->profile,profile) && !c->releases[e->to].revoked && parent[e->to]<0){parent[e->to]=(int)at;queue[tail++]=e->to;}}}
    if(parent[target]<0)return fail(ERROR_NOT_FOUND);unsigned reversed[L4_CATALOG_MAX_RELEASES],count=0;
    for(int at=target;at!=start;at=parent[at])reversed[count++]=(unsigned)at;
    route->count=count;for(unsigned i=0;i<count;i++)route->releases[i]=c->releases[reversed[count-i-1]];return true;
}
