#include "route_plan.h"
#include "catalog_internal.h"
#include "journal_internal.h"
#include <stdlib.h>
#include <string.h>
struct L4RoutePlan{L4Catalog* catalog;ULONGLONG now;char current[64],requested[64],arch[8],profile[64];BYTE current_sha256[32];L4CatalogRoute route;bool owner_trusted;};
typedef struct{BYTE* bytes;DWORD size,at;bool ok;} Codec;
static void put(Codec* c,const void* bytes,DWORD size){if(!c->ok || size>c->size-c->at){c->ok=false;return;}memcpy(c->bytes+c->at,bytes,size);c->at+=size;}
static void number(Codec* c,ULONGLONG value,unsigned width){BYTE bytes[8];if(width==4)l4_store_u32(bytes,(DWORD)value);else l4_store_u64(bytes,value);put(c,bytes,width);}
static ULONGLONG read_number(Codec* c,unsigned width){if(!c->ok || width>c->size-c->at){c->ok=false;return 0;}ULONGLONG n=width==4?l4_store_get32(c->bytes+c->at):l4_store_get64(c->bytes+c->at);c->at+=width;return n;}
static void string(Codec* c,const char* text){size_t size=strlen(text);number(c,size,4);put(c,text,(DWORD)size);}
static void read_string(Codec* c,char* out,DWORD capacity){DWORD size=(DWORD)read_number(c,4);
    if(!c->ok || !size || size>=capacity || size>c->size-c->at || memchr(c->bytes+c->at,0,size)){c->ok=false;return;}
    memcpy(out,c->bytes+c->at,size);out[size]=0;c->at+=size;
}
void l4_route_free(L4RoutePlan* plan){if(plan){l4_catalog_free(plan->catalog);free(plan);}}
bool l4_route_is_owner_trusted(const L4RoutePlan* p){return p && p->owner_trusted;}
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* p){return p?&p->route:NULL;}
const char* l4_route_requested(const L4RoutePlan* p){return p?p->requested:NULL;}
const char* l4_route_arch(const L4RoutePlan* p){return p?p->arch:NULL;}
const char* l4_route_profile(const L4RoutePlan* p){return p?p->profile:NULL;}
ULONGLONG l4_route_revision(const L4RoutePlan* p){return p?p->catalog->revision:0;}
ULONGLONG l4_route_admitted_at(const L4RoutePlan* p){return p?p->now:0;}
const BYTE* l4_route_catalog_sha256(const L4RoutePlan* p){return p?p->catalog->digest:NULL;}
bool l4_route_source(const L4RoutePlan* p,L4CatalogRelease* source){
    if(!source)return l4_store_fail(ERROR_INVALID_PARAMETER);memset(source,0,sizeof(*source));
    if(p)for(unsigned i=0;i<p->catalog->release_count;i++)if(!strcmp(p->catalog->releases[i].version,p->current)){*source=p->catalog->releases[i];return true;}
    return l4_store_fail(ERROR_NOT_FOUND);
}
const BYTE* l4_route_evidence(const L4RoutePlan* p,unsigned step){
    if(!p || step>=p->route.count)return NULL;const char* from=step?p->route.releases[step-1].version:p->current;
    for(unsigned i=0;i<p->catalog->transition_count;i++){const L4CatalogTransition* e=&p->catalog->transitions[i];
        if(!strcmp(p->catalog->releases[e->from].version,from) && !strcmp(p->catalog->releases[e->to].version,p->route.releases[step].version) &&
           !strcmp(e->arch,p->arch) && !strcmp(e->profile,p->profile))return e->evidence;}return NULL;
}
static bool exists(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    (void)sequence;(void)bytes;(void)size;if(kind==L4_RECORD_ROUTE_PLAN)++*(unsigned*)context;return true;
}
static bool save(L4Journal* j,const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* key,DWORD key_size,const char* key_id,bool trusted,ULONGLONG now,const char* current,const BYTE current_hash[32],
    const char* requested,const char* arch,const char* profile,ULONGLONG* sequence){
    if(!sequence)return l4_store_fail(ERROR_INVALID_PARAMETER);*sequence=0;
    if(!j || !current || !requested || !arch || !profile || !current_hash || strlen(current)>=64 || strlen(requested)>=64 || strlen(arch)>=8 || strlen(profile)>=64)
        return l4_store_fail(ERROR_INVALID_PARAMETER);
    unsigned found=0;if(!l4_journal_replay(j,exists,&found))return false;if(found)return l4_store_fail(ERROR_ALREADY_EXISTS);
    L4Catalog* catalog=NULL;L4CatalogRoute route;
    bool ok=trusted?l4_catalog_parse_trusted(bytes,size,signature,signature_size,now,&catalog):l4_catalog_parse_signed(bytes,size,signature,signature_size,key,key_size,key_id,now,&catalog);
    if(ok)ok=l4_catalog_route(catalog,current,current_hash,requested,arch,profile,&route);
    BYTE* encoded=NULL;Codec c={0};
    if(ok){DWORD capacity=size+signature_size+512;encoded=(BYTE*)malloc(capacity);ok=encoded!=NULL;if(!ok)SetLastError(ERROR_NOT_ENOUGH_MEMORY);else{c.bytes=encoded;c.size=capacity;c.ok=true;}}
    if(ok){number(&c,1,4);number(&c,now,8);put(&c,current_hash,32);string(&c,current);string(&c,requested);string(&c,arch);string(&c,profile);
        number(&c,size,4);put(&c,bytes,size);number(&c,signature_size,4);put(&c,signature,signature_size);ok=c.ok;}
    if(ok)ok=l4_catalog_accept(j,catalog,now) && l4_journal_append(j,L4_RECORD_ROUTE_PLAN,encoded,c.at,sequence);
    DWORD error=GetLastError();free(encoded);l4_catalog_free(catalog);return ok?true:l4_store_fail(error?error:ERROR_INVALID_DATA);
}
bool l4_route_save_trusted(L4Journal* j,const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,ULONGLONG now,
    const char* current,const BYTE current_hash[32],const char* requested,const char* arch,const char* profile,ULONGLONG* sequence){
    return save(j,bytes,size,signature,signature_size,NULL,0,NULL,true,now,current,current_hash,requested,arch,profile,sequence);
}
bool l4_route_save_signed(L4Journal* j,const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,const BYTE* key,DWORD key_size,
    const char* key_id,ULONGLONG now,const char* current,const BYTE current_hash[32],const char* requested,const char* arch,const char* profile,ULONGLONG* sequence){
    return save(j,bytes,size,signature,signature_size,key,key_size,key_id,false,now,current,current_hash,requested,arch,profile,sequence);
}
static bool decode(const void* data,DWORD size,const BYTE* key,DWORD key_size,const char* key_id,bool trusted,L4RoutePlan** result){
    if(!result)return l4_store_fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!data || !size || size>L4_JOURNAL_MAX_RECORD)return l4_store_fail(ERROR_INVALID_DATA);
    const BYTE* encoded=data;L4RoutePlan* p=(L4RoutePlan*)calloc(1,sizeof(*p));if(!p)return l4_store_fail(ERROR_NOT_ENOUGH_MEMORY);Codec c={(BYTE*)encoded,size,0,true};
    if(read_number(&c,4)!=1)c.ok=false;p->now=read_number(&c,8);
    if(!c.ok || 32>c.size-c.at)c.ok=false;else{memcpy(p->current_sha256,encoded+c.at,32);c.at+=32;}
    read_string(&c,p->current,sizeof(p->current));read_string(&c,p->requested,sizeof(p->requested));read_string(&c,p->arch,sizeof(p->arch));read_string(&c,p->profile,sizeof(p->profile));
    DWORD doc_size=(DWORD)read_number(&c,4),doc_at=c.at;
    if(!c.ok || !doc_size || doc_size>L4_METADATA_MAX_BYTES || doc_size>c.size-c.at)c.ok=false;else c.at+=doc_size;
    DWORD sig_size=(DWORD)read_number(&c,4),sig_at=c.at;
    bool ok=c.ok && sig_size==L4_METADATA_SIGNATURE_BYTES && sig_size==c.size-c.at;
    if(ok)ok=trusted?l4_catalog_parse_trusted(encoded+doc_at,doc_size,encoded+sig_at,sig_size,p->now,&p->catalog):
        l4_catalog_parse_signed(encoded+doc_at,doc_size,encoded+sig_at,sig_size,key,key_size,key_id,p->now,&p->catalog);
    if(ok)ok=l4_catalog_route(p->catalog,p->current,p->current_sha256,p->requested,p->arch,p->profile,&p->route);
    DWORD error=GetLastError();if(!ok){l4_route_free(p);return l4_store_fail(error?error:ERROR_INVALID_DATA);}p->owner_trusted=trusted;*result=p;return true;
}
static bool load(L4Journal* j,ULONGLONG sequence,const BYTE* key,DWORD key_size,const char* key_id,bool trusted,L4RoutePlan** result){
    if(!result)return l4_store_fail(ERROR_INVALID_PARAMETER);*result=NULL;BYTE* encoded=NULL;DWORD size=0;
    unsigned found=0;if(!l4_journal_replay(j,exists,&found))return false;if(found!=1)return l4_store_fail(ERROR_INVALID_DATA);
    if(!l4_store_find_record(j,L4_RECORD_ROUTE_PLAN,sequence,&encoded,&size))return false;
    bool ok=decode(encoded,size,key,key_size,key_id,trusted,result);DWORD error=GetLastError();free(encoded);return ok?true:l4_store_fail(error);
}
bool l4_route_load_trusted(L4Journal* j,ULONGLONG sequence,L4RoutePlan** result){return load(j,sequence,NULL,0,NULL,true,result);}
bool l4_route_load_signed(L4Journal* j,ULONGLONG sequence,const BYTE* key,DWORD key_size,const char* key_id,L4RoutePlan** result){return load(j,sequence,key,key_size,key_id,false,result);}
bool l4_route_decode_trusted(const void* bytes,DWORD size,L4RoutePlan** result){return decode(bytes,size,NULL,0,NULL,true,result);}
bool l4_route_decode_signed(const void* bytes,DWORD size,const BYTE* key,DWORD key_size,const char* key_id,L4RoutePlan** result){return decode(bytes,size,key,key_size,key_id,false,result);}
