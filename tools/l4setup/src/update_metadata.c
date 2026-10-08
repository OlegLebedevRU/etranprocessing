#include "update_metadata.h"
#include "../../l4common/journal_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct{BYTE* bytes,*signature;DWORD size,signature_size;} Document;
static bool fail(DWORD error){SetLastError(error);return false;}
static void close_document(Document* d){free(d->bytes);free(d->signature);memset(d,0,sizeof(*d));}
static bool document(WORD port,const char* version,const char* name,const char* signature,DWORD timeout,Document* d){
    memset(d,0,sizeof(*d));if(timeout<100 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);ULONGLONG deadline=GetTickCount64()+timeout;
    bool ok=l4_registry_metadata(port,version,signature,timeout,&d->signature,&d->signature_size);
    ULONGLONG now=GetTickCount64();if(ok && (now>=deadline || deadline-now<100))ok=fail(ERROR_TIMEOUT);
    if(ok)ok=l4_registry_metadata(port,version,name,(DWORD)(deadline-now),&d->bytes,&d->size);
    if(!ok){DWORD error=GetLastError();close_document(d);return fail(error?error:ERROR_INVALID_DATA);}return true;
}
bool setup_update_acquire_route(L4Journal* journal,WORD port,DWORD timeout,const char* current,const BYTE current_hash[32],
    const char* requested,const char* arch,const char* profile,ULONGLONG* sequence){
    if(!sequence)return fail(ERROR_INVALID_PARAMETER);*sequence=0;if(!journal || !current || !current_hash || !requested || !arch || !profile)return fail(ERROR_INVALID_PARAMETER);
    Document d;if(!document(port,NULL,"catalog.json","catalog.json.sig",timeout,&d))return false;
    FILETIME ft;GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER utc;utc.LowPart=ft.dwLowDateTime;utc.HighPart=ft.dwHighDateTime;
    bool ok=utc.QuadPart>=116444736000000000ULL;
    if(ok)ok=l4_route_save_trusted(journal,d.bytes,d.size,d.signature,d.signature_size,(utc.QuadPart-116444736000000000ULL)/10000000,
        current,current_hash,requested,arch,profile,sequence);
    DWORD error=ok?0:GetLastError();close_document(&d);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
bool setup_update_acquire_root(const L4RoutePlan* plan,unsigned step,WORD port,DWORD timeout,SetupRootManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;const L4CatalogRoute* route=l4_route_steps(plan);
    if(!route || step>=route->count)return fail(ERROR_INVALID_PARAMETER);
    if(!l4_route_is_owner_trusted(plan))return fail(ERROR_ACCESS_DENIED);Document d;
    if(!document(port,route->releases[step].version,"l4tools-release.json","l4tools-release.json.sig",timeout,&d))return false;
    bool ok=setup_root_parse_trusted(d.bytes,d.size,d.signature,d.signature_size,&route->releases[step],l4_route_arch(plan),result);
    DWORD error=GetLastError();close_document(&d);return ok?true:fail(error);
}
bool setup_update_acquire_descriptor(const L4RoutePlan* plan,unsigned step,const SetupRootManifest* root,const L4Layout* layout,WORD port,DWORD timeout,SetupManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;const L4CatalogRoute* route=l4_route_steps(plan);const SetupRootAsset* json=setup_root_asset(root,0),*sig=setup_root_asset(root,1);
    if(!route || step>=route->count || !json || !sig || !layout)return fail(ERROR_INVALID_PARAMETER);
    if(!l4_route_is_owner_trusted(plan))return fail(ERROR_ACCESS_DENIED);
    if(!setup_root_matches(root,&route->releases[step],l4_route_arch(plan)))return fail(ERROR_REVISION_MISMATCH);
    /* Root selected for this exact saved hop and arch: compare fixed asset name;
     * layout/version/descriptor/archive agreement is checked by the root gate. */
    char expected[40];sprintf_s(expected,sizeof(expected),"l4tools-layout-%s.json",l4_route_arch(plan));
    if(strcmp(expected,json->name))return fail(ERROR_REVISION_MISMATCH);
    Document d;if(!document(port,route->releases[step].version,json->name,sig->name,timeout,&d))return false;
    bool ok=setup_root_descriptor_trusted(root,d.bytes,d.size,d.signature,d.signature_size,layout,result);
    DWORD error=GetLastError();close_document(&d);return ok?true:fail(error);
}
bool setup_update_prepare_package(const L4RoutePlan* plan,unsigned step,const SetupRootManifest* root,const L4Layout* layout,WORD port,DWORD timeout,L4CachedPackage** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(timeout<100 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;SetupManifest* manifest=NULL;L4CachedPackage* package=NULL;
    if(!setup_update_acquire_descriptor(plan,step,root,layout,port,timeout,&manifest))return false;
    const L4CatalogRoute* route=l4_route_steps(plan);const SetupRootAsset* zip=setup_root_asset(root,2);
    ULONGLONG now=GetTickCount64();bool ok=zip && route && step<route->count;
    if(ok && (now>=deadline || deadline-now<100))ok=fail(ERROR_TIMEOUT);
    if(ok)ok=l4_package_download(layout,port,route->releases[step].version,zip->name,zip->size,zip->sha256,(DWORD)(deadline-now),&package);
    if(ok)ok=setup_manifest_prepare(manifest,l4_package_path(package));
    DWORD error=GetLastError();setup_manifest_free(manifest);
    if(!ok){l4_package_close(package);return fail(error?error:ERROR_INVALID_DATA);}*result=package;return true;
}

typedef struct{SetupRootManifest* root;SetupManifest* manifest;L4CachedPackage* package;} PreparedHop;
struct SetupPreparedPlan{L4RoutePlan* route;PreparedHop hops[L4_CATALOG_MAX_RELEASES];};
typedef struct{ULONGLONG route,complete,sequences[L4_CATALOG_MAX_RELEASES];unsigned count;} PreparedIndex;
void setup_prepared_free(SetupPreparedPlan* p){if(!p)return;for(unsigned i=0;i<L4_CATALOG_MAX_RELEASES;i++){
    setup_root_free(p->hops[i].root);setup_manifest_free(p->hops[i].manifest);l4_package_close(p->hops[i].package);}
    l4_route_free(p->route);free(p);
}
const L4RoutePlan* setup_prepared_route(const SetupPreparedPlan* p){return p?p->route:NULL;}
const SetupManifest* setup_prepared_manifest(const SetupPreparedPlan* p,unsigned step){
    const L4CatalogRoute* r=p?l4_route_steps(p->route):NULL;return r && step<r->count?p->hops[step].manifest:NULL;
}
/* Portable LE codec: schema/route/step/root-size/descriptor-size/leaf-size,
 * exact root +384-byte sig +descriptor +384-byte sig +ASCII GUID.zip leaf.
 * Every bound is checked before a pointer into the record is exposed. */
static bool package_record(const void* bytes,DWORD size,ULONGLONG route,unsigned count,unsigned* step){
    if(!bytes || size<28)return fail(ERROR_INVALID_DATA);const BYTE* b=(const BYTE*)bytes;
    DWORD root=l4_store_get32(b+16),desc=l4_store_get32(b+20),leaf=l4_store_get32(b+24);*step=l4_store_get32(b+12);
    return l4_store_get32(b)==1 && l4_store_get64(b+4)==route && *step<count &&
        root && root<=L4_METADATA_MAX_BYTES && desc && desc<=L4_METADATA_MAX_BYTES && leaf==42 &&
        size==28+root+desc+2*L4_METADATA_SIGNATURE_BYTES+leaf?true:fail(ERROR_INVALID_DATA);
}
static bool prepared_visit(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    PreparedIndex* index=(PreparedIndex*)context;const BYTE* b=(const BYTE*)bytes;
    if(kind==L4_RECORD_PREPARED_PACKAGE){unsigned step;
        if(index->complete || !package_record(bytes,size,index->route,index->count,&step) || index->sequences[step])return fail(ERROR_INVALID_DATA);
        index->sequences[step]=sequence;
    }else if(kind==L4_RECORD_PACKAGES_COMPLETE){
        if(index->complete || size!=16+index->count*8 || l4_store_get32(b)!=1 || l4_store_get64(b+4)!=index->route || l4_store_get32(b+12)!=index->count)return fail(ERROR_INVALID_DATA);
        for(unsigned i=0;i<index->count;i++)if(!index->sequences[i] || l4_store_get64(b+16+i*8)!=index->sequences[i])return fail(ERROR_INVALID_DATA);
        index->complete=sequence;
    }return true;
}
static bool hop_layout(L4Journal* j,const L4RoutePlan* plan,unsigned step,L4Layout* layout){
    const L4CatalogRoute* r=l4_route_steps(plan);wchar_t version[64];
    return r && step<r->count && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,r->releases[step].version,-1,version,64) &&
        l4_layout_from_roots(layout,j->layout.binaries,j->layout.data,version)?true:fail(ERROR_INVALID_PARAMETER);
}
static bool restore_hop_record(const L4Layout* roots,ULONGLONG route_sequence,const L4RoutePlan* route,unsigned step,const BYTE* b,DWORD size,PreparedHop* hop){

    const L4CatalogRoute* r=l4_route_steps(route);unsigned saved_step;L4Layout layout;wchar_t version[64];
    bool ok=r && package_record(b,size,route_sequence,r->count,&saved_step) && saved_step==step && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,r->releases[step].version,-1,version,64)&&l4_layout_from_roots(&layout,roots->binaries,roots->data,version);
    if(ok){DWORD root_size=l4_store_get32(b+16),desc_size=l4_store_get32(b+20);const BYTE* root=b+28,*root_sig=root+root_size,*desc=root_sig+L4_METADATA_SIGNATURE_BYTES,*desc_sig=desc+desc_size;
        wchar_t leaf[43];const BYTE* raw_leaf=desc_sig+L4_METADATA_SIGNATURE_BYTES;
        for(unsigned i=0;i<42;i++)leaf[i]=raw_leaf[i];leaf[42]=0;
        ok=setup_root_parse_trusted(root,root_size,root_sig,L4_METADATA_SIGNATURE_BYTES,&r->releases[step],l4_route_arch(route),&hop->root) &&
            setup_root_descriptor_trusted(hop->root,desc,desc_size,desc_sig,L4_METADATA_SIGNATURE_BYTES,&layout,&hop->manifest);
        const SetupRootAsset* zip=setup_root_asset(hop->root,2);
        if(ok)ok=zip && l4_package_reopen(&layout,leaf,zip->size,zip->sha256,&hop->package) && setup_manifest_verify(hop->manifest);
    }DWORD error=GetLastError();return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
static bool restore_hop(L4Journal* j,ULONGLONG route_sequence,const L4RoutePlan* route,unsigned step,ULONGLONG sequence,PreparedHop* hop){
 BYTE* b=NULL;DWORD size=0;if(!l4_store_find_record(j,L4_RECORD_PREPARED_PACKAGE,sequence,&b,&size))return false;
 bool ok=restore_hop_record(&j->layout,route_sequence,route,step,b,size,hop);DWORD error=GetLastError();free(b);return ok?true:fail(error);
}
static bool prep_checkpoint(ULONGLONG deadline,const volatile LONG* cancelled);
static bool new_prepared(L4Journal* j,ULONGLONG route_sequence,SetupPreparedPlan** result,PreparedIndex* index,ULONGLONG deadline,const volatile LONG* cancelled){
    *result=NULL;memset(index,0,sizeof(*index));if(!j || !route_sequence)return fail(ERROR_INVALID_PARAMETER);
    SetupPreparedPlan* p=(SetupPreparedPlan*)calloc(1,sizeof(*p));if(!p)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=l4_route_load_trusted(j,route_sequence,&p->route) && l4_route_is_owner_trusted(p->route);
    const L4CatalogRoute* r=l4_route_steps(p->route);if(ok){index->route=route_sequence;index->count=r->count;ok=l4_journal_replay(j,prepared_visit,index);}
    for(unsigned i=0;ok && i<index->count;i++)if(index->sequences[i])ok=(!deadline || prep_checkpoint(deadline,cancelled)) && restore_hop(j,route_sequence,p->route,i,index->sequences[i],&p->hops[i]);
    if(ok && deadline)ok=prep_checkpoint(deadline,cancelled);
    DWORD error=GetLastError();if(!ok){setup_prepared_free(p);return fail(error?error:ERROR_INVALID_DATA);}*result=p;return true;
}
bool setup_update_load_prepared(L4Journal* j,ULONGLONG sequence,SetupPreparedPlan** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;BYTE* b=NULL;DWORD size=0;
    if(!j || !sequence)return fail(ERROR_INVALID_PARAMETER);
    if(!l4_store_find_record(j,L4_RECORD_PACKAGES_COMPLETE,sequence,&b,&size))return false;
    bool ok=size>=16 && l4_store_get32(b)==1;ULONGLONG route=ok?l4_store_get64(b+4):0;free(b);
    SetupPreparedPlan* p=NULL;PreparedIndex index={0};if(ok)ok=new_prepared(j,route,&p,&index,0,NULL);
    if(ok && index.complete!=sequence)ok=fail(ERROR_INVALID_DATA);
    DWORD error=GetLastError();if(!ok){setup_prepared_free(p);return fail(error?error:ERROR_INVALID_DATA);}*result=p;return true;
}
static DWORD remaining(ULONGLONG deadline){ULONGLONG now=GetTickCount64();if(now>=deadline || deadline-now<100){SetLastError(ERROR_TIMEOUT);return 0;}return (DWORD)(deadline-now);}
static bool prep_checkpoint(ULONGLONG deadline,const volatile LONG* cancelled){
    if(cancelled && InterlockedCompareExchange((volatile LONG*)cancelled,0,0))return fail(ERROR_CANCELLED);
    return GetTickCount64()<deadline?true:fail(ERROR_TIMEOUT);
}
static bool acquire_hop(L4Journal* j,ULONGLONG route_sequence,SetupPreparedPlan* p,unsigned step,WORD port,ULONGLONG deadline,const volatile LONG* cancelled,ULONGLONG* sequence){
    const L4CatalogRoute* r=l4_route_steps(p->route);Document root={0},desc={0};PreparedHop* hop=&p->hops[step];L4Layout layout;
    DWORD budget=remaining(deadline);bool ok=budget && hop_layout(j,p->route,step,&layout) &&
        document(port,r->releases[step].version,"l4tools-release.json","l4tools-release.json.sig",budget,&root);
    if(ok)ok=prep_checkpoint(deadline,cancelled) && setup_root_parse_trusted(root.bytes,root.size,root.signature,root.signature_size,&r->releases[step],l4_route_arch(p->route),&hop->root);
    const SetupRootAsset* json=setup_root_asset(hop->root,0),*sig=setup_root_asset(hop->root,1),*zip=setup_root_asset(hop->root,2);
    if(ok){budget=remaining(deadline);ok=prep_checkpoint(deadline,cancelled) && budget && document(port,r->releases[step].version,json->name,sig->name,budget,&desc);}
    if(ok)ok=prep_checkpoint(deadline,cancelled) && setup_root_descriptor_trusted(hop->root,desc.bytes,desc.size,desc.signature,desc.signature_size,&layout,&hop->manifest);
    if(ok){budget=remaining(deadline);ok=prep_checkpoint(deadline,cancelled) && budget && l4_package_download(&layout,port,r->releases[step].version,zip->name,zip->size,zip->sha256,budget,&hop->package);}
    if(ok)ok=prep_checkpoint(deadline,cancelled) && setup_manifest_prepare(hop->manifest,l4_package_path(hop->package)) && prep_checkpoint(deadline,cancelled);
    BYTE* b=NULL;DWORD size=28+root.size+desc.size+2*L4_METADATA_SIGNATURE_BYTES+42;
    if(ok){b=(BYTE*)calloc(1,size);ok=b!=NULL;if(!ok)SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
    if(ok){l4_store_u32(b,1);l4_store_u64(b+4,route_sequence);l4_store_u32(b+12,step);l4_store_u32(b+16,root.size);l4_store_u32(b+20,desc.size);l4_store_u32(b+24,42);
        BYTE* at=b+28;memcpy(at,root.bytes,root.size);at+=root.size;memcpy(at,root.signature,L4_METADATA_SIGNATURE_BYTES);at+=L4_METADATA_SIGNATURE_BYTES;
        memcpy(at,desc.bytes,desc.size);at+=desc.size;memcpy(at,desc.signature,L4_METADATA_SIGNATURE_BYTES);at+=L4_METADATA_SIGNATURE_BYTES;
        const wchar_t* leaf=l4_package_leaf(hop->package);for(unsigned i=0;i<42;i++)at[i]=(BYTE)leaf[i];
        ok=prep_checkpoint(deadline,cancelled) && l4_journal_append(j,L4_RECORD_PREPARED_PACKAGE,b,size,sequence);
    }DWORD error=GetLastError();free(b);close_document(&root);close_document(&desc);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
bool setup_update_prepare_route(L4Journal* j,ULONGLONG route_sequence,WORD port,DWORD timeout,ULONGLONG* sequence){
    if(sequence)*sequence=0;if(timeout<100 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    return setup_update_prepare_route_controlled(j,route_sequence,port,GetTickCount64()+timeout,NULL,sequence);
}
bool setup_update_prepare_route_controlled(L4Journal* j,ULONGLONG route_sequence,WORD port,ULONGLONG deadline,const volatile LONG* cancelled,ULONGLONG* sequence){
    if(!sequence)return fail(ERROR_INVALID_PARAMETER);*sequence=0;
    ULONGLONG now=GetTickCount64();if(!port || deadline<=now || deadline-now>600000)return fail(ERROR_INVALID_PARAMETER);
    if(!prep_checkpoint(deadline,cancelled))return false;
    SetupPreparedPlan* p=NULL;PreparedIndex index;if(!new_prepared(j,route_sequence,&p,&index,deadline,cancelled))return false;
    bool ok=prep_checkpoint(deadline,cancelled);
    if(ok && index.complete){ULONGLONG complete=index.complete;setup_prepared_free(p);
        if(!prep_checkpoint(deadline,cancelled))return false;*sequence=complete;return true;}
    for(unsigned i=0;ok && i<index.count;i++)if(!index.sequences[i])ok=prep_checkpoint(deadline,cancelled) && acquire_hop(j,route_sequence,p,i,port,deadline,cancelled,&index.sequences[i]);
    /* All cache pins are still held; recheck every immutable release immediately
     * before the durable completion. No false 'prepared' after an earlier hop
     * changed while a later hop was downloaded. Service readiness is separate. */
    for(unsigned i=0;ok && i<index.count;i++)ok=prep_checkpoint(deadline,cancelled) && setup_manifest_verify(p->hops[i].manifest);
    if(ok)ok=prep_checkpoint(deadline,cancelled);
    if(ok){BYTE b[16+8*L4_CATALOG_MAX_RELEASES];l4_store_u32(b,1);l4_store_u64(b+4,route_sequence);l4_store_u32(b+12,index.count);
        for(unsigned i=0;i<index.count;i++)l4_store_u64(b+16+i*8,index.sequences[i]);ok=l4_journal_append(j,L4_RECORD_PACKAGES_COMPLETE,b,16+8*index.count,sequence);}
    DWORD error=GetLastError();setup_prepared_free(p);
    if(ok && !prep_checkpoint(deadline,cancelled)){error=GetLastError();ok=false;}
    if(!ok)*sequence=0;return ok?true:fail(error?error:ERROR_INVALID_DATA);
}

struct SetupOperationPlan{SetupPreparedPlan* packages;SetupRootManifest* root;SetupManifest* source;
    unsigned count,config_count;ULONGLONG sequence,configs[L4_OPERATION_CONFIG_LIMIT],references[L4_CATALOG_MAX_RELEASES][4];
    L4Layout owner_layout;wchar_t owner_directory[MAX_PATH];BYTE operation_digest[32];bool immutable_snapshot;
    L4ServiceSwitch switches[L4_CATALOG_MAX_RELEASES][4];};
void setup_operation_free(SetupOperationPlan* p){if(!p)return;setup_prepared_free(p->packages);setup_root_free(p->root);setup_manifest_free(p->source);free(p);}
const SetupManifest* setup_operation_source(const SetupOperationPlan* p){return p?p->source:NULL;}
const SetupRootManifest* setup_operation_root(const SetupOperationPlan* p){return p?p->root:NULL;}
const L4RoutePlan* setup_operation_route(const SetupOperationPlan* p){return p?setup_prepared_route(p->packages):NULL;}
const SetupManifest* setup_operation_target(const SetupOperationPlan* p,unsigned hop){return p && hop<p->count?setup_prepared_manifest(p->packages,hop):NULL;}
const SetupRootManifest* setup_operation_target_root(const SetupOperationPlan* p,unsigned hop){return p&&p->packages&&hop<p->count?p->packages->hops[hop].root:NULL;}
const ULONGLONG* setup_operation_configs(const SetupOperationPlan* p,unsigned* count){if(count)*count=p?p->config_count:0;return p?p->configs:NULL;}
unsigned setup_operation_hops(const SetupOperationPlan* p){return p?p->count:0;}
ULONGLONG setup_operation_sequence(const SetupOperationPlan* p){return p?p->sequence:0;}
ULONGLONG setup_operation_switch_reference(const SetupOperationPlan* p,unsigned h,unsigned s){return p && h<p->count && s<4?p->references[h][s]:0;}
bool setup_operation_binding(const SetupOperationPlan* p,L4Journal* j,ULONGLONG sequence){
    if(!p || p->immutable_snapshot || !j || !j->lock || j->poisoned || !sequence || sequence!=p->sequence ||
        memcmp(&p->owner_layout,&j->layout,sizeof(j->layout)) || wcscmp(p->owner_directory,j->directory))return fail(ERROR_INVALID_STATE);
    BYTE* b=NULL,hash[32];DWORD size=0;bool ok=l4_store_find_record(j,L4_RECORD_OPERATION_PLAN,sequence,&b,&size) &&
        l4_store_hash(b,size,NULL,0,hash) && !memcmp(hash,p->operation_digest,32);free(b);return ok?true:fail(ERROR_CRC);
}
bool setup_operation_verify_images(const SetupOperationPlan* p){
    if(!p || !p->source || !p->packages || !setup_manifest_verify(p->source))return false;
    for(unsigned i=0;i<p->count;i++)if(!setup_manifest_verify(setup_prepared_manifest(p->packages,i)))return false;return true;
}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* p,unsigned hop,unsigned service){return p && hop<p->count && service<4?&p->switches[hop][service]:NULL;}
typedef struct{ULONGLONG source,complete,packages;} OperationIndex;
static bool operation_visit(DWORD kind,ULONGLONG sequence,const void* bytes,DWORD size,void* context){
    OperationIndex* index=(OperationIndex*)context;const BYTE* b=(const BYTE*)bytes;
    if(kind==L4_RECORD_INSTALLED_SOURCE){
        if(index->source || index->complete || size<20 || l4_store_get32(b)!=1 || l4_store_get64(b+4)!=index->packages)return fail(ERROR_INVALID_DATA);
        DWORD root=l4_store_get32(b+12),desc=l4_store_get32(b+16);
        if(!root || root>L4_METADATA_MAX_BYTES || !desc || desc>L4_METADATA_MAX_BYTES || size!=20+root+desc+2*L4_METADATA_SIGNATURE_BYTES)return fail(ERROR_INVALID_DATA);
        index->source=sequence;
    }else if(kind==L4_RECORD_OPERATION_PLAN){
        if(index->complete || !index->source || size<28 || l4_store_get32(b)!=1 || l4_store_get64(b+4)!=index->packages || l4_store_get64(b+12)!=index->source)return fail(ERROR_INVALID_DATA);
        index->complete=sequence;
    }return true;
}
static bool source_documents_roots(const L4Layout* roots,SetupOperationPlan* p,const Document* root,const Document* desc){
    L4CatalogRelease source;const L4RoutePlan* route=setup_prepared_route(p->packages);wchar_t version[64];L4Layout layout;
    if(!l4_route_source(route,&source) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,source.version,-1,version,64) ||
        !l4_layout_from_roots(&layout,roots->binaries,roots->data,version))return fail(ERROR_INVALID_DATA);
    /* Revocation forbids a TARGET. Authenticate the already installed source
     * against its pinned owner root so a permitted route can leave it. */
    source.revoked=false;
    return setup_root_parse_trusted(root->bytes,root->size,root->signature,root->signature_size,&source,l4_route_arch(route),&p->root) &&
        setup_root_descriptor_trusted(p->root,desc->bytes,desc->size,desc->signature,desc->signature_size,&layout,&p->source) && setup_manifest_verify(p->source);
}
static bool restore_source_record(const L4Layout* roots,SetupOperationPlan* p,BYTE* b,DWORD size){

    bool ok=size>=20;Document root={0},desc={0};
    if(ok){root.size=l4_store_get32(b+12);desc.size=l4_store_get32(b+16);
        ok=root.size && root.size<=L4_METADATA_MAX_BYTES && desc.size && desc.size<=L4_METADATA_MAX_BYTES && size==20+root.size+desc.size+2*L4_METADATA_SIGNATURE_BYTES;}
    if(ok){root.bytes=b+20;root.signature=root.bytes+root.size;root.signature_size=L4_METADATA_SIGNATURE_BYTES;
        desc.bytes=root.signature+L4_METADATA_SIGNATURE_BYTES;desc.signature=desc.bytes+desc.size;desc.signature_size=L4_METADATA_SIGNATURE_BYTES;ok=source_documents_roots(roots,p,&root,&desc);}
    DWORD error=GetLastError();return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
static bool restore_source(L4Journal* j,SetupOperationPlan* p,ULONGLONG sequence){
 BYTE* b=NULL;DWORD size=0;if(!l4_store_find_record(j,L4_RECORD_INSTALLED_SOURCE,sequence,&b,&size))return false;
 bool ok=restore_source_record(&j->layout,p,b,size);DWORD error=GetLastError();free(b);return ok?true:fail(error);
}
static bool acquire_source(L4Journal* j,SetupOperationPlan* p,ULONGLONG packages,WORD port,DWORD timeout,ULONGLONG* sequence){
    L4CatalogRelease source;const L4RoutePlan* route=setup_prepared_route(p->packages);if(!l4_route_source(route,&source))return false;
    Document root={0},desc={0};ULONGLONG deadline=GetTickCount64()+timeout;char name[40],signature[44];
    sprintf_s(name,sizeof(name),"l4tools-layout-%s.json",l4_route_arch(route));sprintf_s(signature,sizeof(signature),"%s.sig",name);
    bool ok=document(port,source.version,"l4tools-release.json","l4tools-release.json.sig",timeout,&root);DWORD budget=remaining(deadline);
    if(ok)ok=budget && document(port,source.version,name,signature,budget,&desc);
    if(ok)ok=source_documents_roots(&j->layout,p,&root,&desc);BYTE* b=NULL;DWORD size=20+root.size+desc.size+2*L4_METADATA_SIGNATURE_BYTES;
    if(ok){b=(BYTE*)malloc(size);ok=b!=NULL;if(!ok)SetLastError(ERROR_NOT_ENOUGH_MEMORY);}
    if(ok){l4_store_u32(b,1);l4_store_u64(b+4,packages);l4_store_u32(b+12,root.size);l4_store_u32(b+16,desc.size);BYTE* at=b+20;
        memcpy(at,root.bytes,root.size);at+=root.size;memcpy(at,root.signature,L4_METADATA_SIGNATURE_BYTES);at+=L4_METADATA_SIGNATURE_BYTES;
        memcpy(at,desc.bytes,desc.size);at+=desc.size;memcpy(at,desc.signature,L4_METADATA_SIGNATURE_BYTES);
        ok=l4_journal_append(j,L4_RECORD_INSTALLED_SOURCE,b,size,sequence);}
    DWORD error=GetLastError();free(b);close_document(&root);close_document(&desc);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
static bool operation_configs(L4Journal* j,const ULONGLONG* configs,unsigned count){
    for(unsigned i=0;i<count;i++){if(!configs[i])return fail(ERROR_INVALID_PARAMETER);for(unsigned k=0;k<i;k++)if(configs[k]==configs[i])return fail(ERROR_DUP_NAME);
        if(!l4_config_verify(j,configs[i],false))return false;}return true;
}
static bool derive_switches_from(SetupOperationPlan* p,const L4ServiceInventory* saved){
    const wchar_t* services[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv"};const wchar_t* exes[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe"};
    L4ServiceInventory initial[4];const L4Layout* source_layout=setup_manifest_layout(p->source);
    for(unsigned s=0;s<4;s++){wchar_t image[MAX_PATH],prefix[MAX_PATH+3];
        if(saved)initial[s]=saved[s];else if(!l4_service_inventory(services[s],&initial[s]))return false;
        if(!initial[s].installed || !l4_layout_component(source_layout,components[s],exes[s],image))return fail(ERROR_INVALID_DATA);
        swprintf_s(prefix,_countof(prefix),L"\"%ls\"",image);size_t n=wcslen(prefix);
        if(wcsncmp(prefix,initial[s].image_path,n) || (initial[s].image_path[n] && initial[s].image_path[n]!=L' ' && initial[s].image_path[n]!=L'\t') ||
            _wcsicmp(initial[s].account,L"LocalSystem") || (initial[s].start_type!=SERVICE_AUTO_START && initial[s].start_type!=SERVICE_DEMAND_START))return fail(ERROR_REVISION_MISMATCH);
    }
    p->count=l4_route_steps(setup_prepared_route(p->packages))->count;
    for(unsigned hop=0;hop<p->count;hop++){
        const SetupManifest* target=setup_prepared_manifest(p->packages,hop),*before=hop?setup_prepared_manifest(p->packages,hop-1):p->source;unsigned target_count,before_count;
        const L4ReleaseFile* target_files=setup_manifest_files(target,&target_count),*before_files=setup_manifest_files(before,&before_count);
        for(unsigned s=0;s<4;s++){L4ServiceInventory expected=initial[s];if(hop)wcscpy_s(expected.image_path,2048,p->switches[hop-1][s].after);
            if(!l4_service_switch_plan(setup_manifest_layout(target),services[s],&expected,NULL,target_files,target_count,before_files,before_count,&p->switches[hop][s]))return false;}
    }return true;
}
static bool derive_switches(SetupOperationPlan* p){return derive_switches_from(p,NULL);}
static bool open_operation(L4Journal* j,ULONGLONG packages,SetupOperationPlan** result,OperationIndex* index){
    *result=NULL;memset(index,0,sizeof(*index));index->packages=packages;
    SetupOperationPlan* p=(SetupOperationPlan*)calloc(1,sizeof(*p));if(!p)return fail(ERROR_NOT_ENOUGH_MEMORY);
    bool ok=j && !j->poisoned && setup_update_load_prepared(j,packages,&p->packages) && l4_journal_replay(j,operation_visit,index);
    if(ok && index->source)ok=restore_source(j,p,index->source);
    DWORD error=GetLastError();if(!ok){setup_operation_free(p);return fail(error?error:ERROR_INVALID_DATA);}*result=p;return true;
}
static bool load_operation(L4Journal* j,ULONGLONG sequence,bool pinned,SetupOperationPlan** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;if(!j || !sequence)return fail(ERROR_INVALID_PARAMETER);BYTE* b=NULL;DWORD size;
    if(!l4_store_find_record(j,L4_RECORD_OPERATION_PLAN,sequence,&b,&size))return false;
    SetupOperationPlan* p=NULL;OperationIndex index={0};bool ok=size>=28 && l4_store_get32(b)==1;
    if(ok)ok=open_operation(j,l4_store_get64(b+4),&p,&index) && index.complete==sequence;
    if(ok){p->count=l4_route_steps(setup_prepared_route(p->packages))->count;p->config_count=l4_store_get32(b+24);
        ok=l4_store_get32(b+20)==p->count && p->config_count<=L4_OPERATION_CONFIG_LIMIT && size==28+8*p->config_count+32*p->count;}
    L4ServiceInventory original[4];memset(original,0,sizeof(original));
    if(ok && pinned && p->count)for(unsigned s=0;ok && s<4;s++){
        L4ServiceSwitch saved;ULONGLONG ref=l4_store_get64(b+28+8*p->config_count+8*s);
        ok=ref && ref<sequence && l4_journal_load_switch(j,ref,&saved);if(ok)original[s]=saved.before;
    }
    if(ok)ok=derive_switches_from(p,pinned?original:NULL);
    if(ok){for(unsigned i=0;i<p->config_count;i++){p->configs[i]=l4_store_get64(b+28+i*8);if(p->configs[i]>=sequence)ok=false;}
        for(unsigned i=0;ok && i<p->config_count;i++){
            for(unsigned k=0;ok && k<i;k++)if(p->configs[k]==p->configs[i])ok=false;
            if(pinned){BYTE* raw=NULL;DWORD length=0;ok=ok && p->configs[i] && l4_store_find_record(j,20,p->configs[i],&raw,&length) && length>0;free(raw);}
        }
        if(ok && !pinned)ok=operation_configs(j,p->configs,p->config_count);}
    ULONGLONG seen[4*L4_CATALOG_MAX_RELEASES];unsigned used=0;
    for(unsigned h=0;ok && h<p->count;h++)for(unsigned s=0;ok && s<4;s++){
        ULONGLONG ref=l4_store_get64(b+28+8*p->config_count+(h*4+s)*8);L4ServiceSwitch saved;
        if(!ref || ref>=sequence)ok=false;for(unsigned i=0;ok && i<used;i++)if(seen[i]==ref)ok=false;
        if(ok)ok=l4_journal_load_switch(j,ref,&saved) && !memcmp(&saved,&p->switches[h][s],sizeof(saved));if(ok){seen[used++]=ref;p->references[h][s]=ref;}
    }
    if(ok){p->sequence=sequence;p->owner_layout=j->layout;wcscpy_s(p->owner_directory,MAX_PATH,j->directory);ok=l4_store_hash(b,size,NULL,0,p->operation_digest);}
    DWORD error=GetLastError();free(b);if(!ok){setup_operation_free(p);return fail(error?error:ERROR_INVALID_DATA);}*result=p;return true;
}
bool setup_update_load_operation(L4Journal* j,ULONGLONG s,SetupOperationPlan** out){return load_operation(j,s,false,out);}
bool setup_update_load_operation_pinned(L4Journal* j,ULONGLONG s,SetupOperationPlan** out){return load_operation(j,s,true,out);}
static bool snapshot_switch(const L4JournalReader* reader,const L4Layout* roots,ULONGLONG ref,L4ServiceSwitch* p){
 BYTE* b=NULL;DWORD n=0;if(!l4_journal_reader_find(reader,10,ref,&b,&n))return false;bool ok=l4_switch_decode(roots,b,n,p);DWORD error=GetLastError();free(b);return ok?true:fail(error);
}
static bool snapshot_operation_open(const L4JournalReader* reader,const L4Layout* roots,ULONGLONG packages,SetupOperationPlan** out,OperationIndex* index){
 *out=NULL;memset(index,0,sizeof(*index));index->packages=packages;SetupOperationPlan* p=calloc(1,sizeof(*p));if(!p)return fail(ERROR_OUTOFMEMORY);
 p->packages=calloc(1,sizeof(*p->packages));if(!p->packages){setup_operation_free(p);return fail(ERROR_OUTOFMEMORY);}BYTE* b=NULL;DWORD n=0;
 bool ok=l4_journal_reader_find(reader,62,packages,&b,&n)&&n>=16&&l4_store_get32(b)==1;ULONGLONG route=ok?l4_store_get64(b+4):0;free(b);b=NULL;
 if(ok)ok=route&&route<packages&&l4_journal_reader_find(reader,60,route,&b,&n)&&l4_route_decode_trusted(b,n,&p->packages->route)&&l4_route_is_owner_trusted(p->packages->route);free(b);b=NULL;
 PreparedIndex prepared={0};const L4CatalogRoute* steps=setup_prepared_route(p->packages)?l4_route_steps(p->packages->route):NULL;
 if(ok){prepared.route=route;prepared.count=steps->count;ok=l4_journal_reader_replay(reader,prepared_visit,&prepared)&&prepared.complete==packages;}
 for(unsigned h=0;ok&&h<prepared.count;h++){
  ok=prepared.sequences[h]&&prepared.sequences[h]<packages&&l4_journal_reader_find(reader,61,prepared.sequences[h],&b,&n)&&restore_hop_record(roots,route,p->packages->route,h,b,n,&p->packages->hops[h]);free(b);b=NULL;
 }
 if(ok)ok=l4_journal_reader_replay(reader,operation_visit,index)&&index->source&&index->source<index->complete&&packages<index->source&&l4_journal_reader_find(reader,63,index->source,&b,&n)&&restore_source_record(roots,p,b,n);free(b);
 DWORD error=GetLastError();if(!ok){setup_operation_free(p);return fail(error?error:ERROR_INVALID_DATA);}*out=p;return true;
}
bool setup_update_load_operation_snapshot(const L4JournalReader* reader,const L4Layout* roots,ULONGLONG sequence,SetupOperationPlan** out){
 if(!out)return fail(ERROR_INVALID_PARAMETER);*out=NULL;if(!reader||!roots||!sequence)return fail(ERROR_INVALID_PARAMETER);if(!l4_journal_reader_is_immutable(reader))return fail(ERROR_ACCESS_DENIED);BYTE* b=NULL;DWORD n=0;
 if(!l4_journal_reader_find(reader,64,sequence,&b,&n))return false;SetupOperationPlan* p=NULL;OperationIndex index={0};
 bool ok=n>=28&&l4_store_get32(b)==1&&snapshot_operation_open(reader,roots,l4_store_get64(b+4),&p,&index)&&index.complete==sequence;
 if(ok){p->count=l4_route_steps(p->packages->route)->count;p->config_count=l4_store_get32(b+24);ok=p->count&&p->count<=L4_CATALOG_MAX_RELEASES&&l4_store_get32(b+20)==p->count&&p->config_count<=L4_OPERATION_CONFIG_LIMIT&&n==28+8*p->config_count+32*p->count;}
 L4ServiceInventory initial[4];memset(initial,0,sizeof(initial));for(unsigned s=0;ok&&s<4;s++){L4ServiceSwitch v;ULONGLONG ref=l4_store_get64(b+28+8*p->config_count+8*s);ok=ref&&ref<sequence&&snapshot_switch(reader,roots,ref,&v);if(ok)initial[s]=v.before;}
 if(ok)ok=derive_switches_from(p,initial);
 for(unsigned i=0;ok&&i<p->config_count;i++){p->configs[i]=l4_store_get64(b+28+i*8);ok=p->configs[i]&&p->configs[i]<sequence;for(unsigned k=0;ok&&k<i;k++)ok=p->configs[k]!=p->configs[i];BYTE* config=NULL;DWORD size=0;if(ok)ok=l4_journal_reader_find(reader,20,p->configs[i],&config,&size)&&size>0;free(config);}
 ULONGLONG used[4*L4_CATALOG_MAX_RELEASES];unsigned count=0;for(unsigned h=0;ok&&h<p->count;h++)for(unsigned s=0;ok&&s<4;s++){
  ULONGLONG ref=l4_store_get64(b+28+8*p->config_count+(h*4+s)*8);L4ServiceSwitch v;ok=ref&&ref<sequence;for(unsigned i=0;ok&&i<count;i++)ok=ref!=used[i];if(ok)ok=snapshot_switch(reader,roots,ref,&v)&&!memcmp(&v,&p->switches[h][s],sizeof(v));if(ok){used[count++]=ref;p->references[h][s]=ref;}
 }
 if(ok){const wchar_t* directory=l4_journal_reader_directory(reader);ok=directory&&wcslen(directory)<MAX_PATH;if(ok){p->sequence=sequence;p->owner_layout=*roots;p->immutable_snapshot=true;wcscpy_s(p->owner_directory,MAX_PATH,directory);ok=l4_store_hash(b,n,NULL,0,p->operation_digest);}}
 DWORD error=GetLastError();free(b);if(!ok){setup_operation_free(p);return fail(error?error:ERROR_INVALID_DATA);}*out=p;return true;
}
bool setup_update_plan_operation(L4Journal* j,ULONGLONG packages,const ULONGLONG* configs,unsigned count,WORD port,DWORD timeout,ULONGLONG* sequence){
    if(!sequence)return fail(ERROR_INVALID_PARAMETER);*sequence=0;if(!j || !packages || count>L4_OPERATION_CONFIG_LIMIT || (count && !configs) || !port || timeout<100 || timeout>600000)return fail(ERROR_INVALID_PARAMETER);
    SetupOperationPlan* p=NULL;OperationIndex index;if(!open_operation(j,packages,&p,&index))return false;
    if(index.complete){setup_operation_free(p);p=NULL;bool ok=setup_update_load_operation(j,index.complete,&p);
        if(ok)ok=p->config_count==count && (!count || !memcmp(p->configs,configs,count*sizeof(ULONGLONG)));
        if(ok)*sequence=index.complete;setup_operation_free(p);return ok?true:fail(ERROR_REVISION_MISMATCH);}
    bool ok=operation_configs(j,configs,count);if(ok && !index.source)ok=acquire_source(j,p,packages,port,timeout,&index.source);
    if(ok)ok=derive_switches(p);BYTE b[28+8*L4_OPERATION_CONFIG_LIMIT+32*L4_CATALOG_MAX_RELEASES];memset(b,0,sizeof(b));
    if(ok){l4_store_u32(b,1);l4_store_u64(b+4,packages);l4_store_u64(b+12,index.source);l4_store_u32(b+20,p->count);l4_store_u32(b+24,count);for(unsigned i=0;i<count;i++)l4_store_u64(b+28+i*8,configs[i]);}
    for(unsigned h=0;ok && h<p->count;h++)for(unsigned s=0;ok && s<4;s++){ULONGLONG ref;
        ok=l4_journal_save_switch(j,&p->switches[h][s],&ref);if(ok)l4_store_u64(b+28+count*8+(h*4+s)*8,ref);}
    if(ok)ok=operation_configs(j,configs,count) && setup_manifest_verify(p->source);
    if(ok)ok=l4_journal_append(j,L4_RECORD_OPERATION_PLAN,b,28+count*8+32*p->count,sequence);
    DWORD error=GetLastError();setup_operation_free(p);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}
bool setup_update_operation_preflight(L4Journal* j,ULONGLONG sequence,const L4AccessActors* actors,const L4BootstrapChecks* checks,DWORD timeout){
    if(!actors || !checks || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    SetupOperationPlan* p=NULL;if(!setup_update_load_operation(j,sequence,&p))return false;
    bool ok=p->count!=0;if(!ok)SetLastError(ERROR_NO_MORE_ITEMS);L4BootstrapPlan source={0};source.layout=*setup_manifest_layout(p->source);
    for(unsigned s=0;ok && s<4;s++){const L4ServiceSwitch* first=&p->switches[0][s];wcscpy_s(source.services[s],32,first->service);
        wcscpy_s(source.commands[s],2048,first->before.image_path);source.start_types[s]=first->before.start_type;source.sizes[s]=first->before_size;memcpy(source.sha256[s],first->before_sha256,32);}
    if(ok)ok=setup_readiness_preflight(j,&source,actors,p->configs,p->config_count,checks,timeout);
    DWORD error=GetLastError();setup_operation_free(p);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}

/* Use actual child socket ownership; a pre-existing health responder is never
 * accepted. Parent closes its own job on every outcome, keeping the EXE pinned. */
#include "../../l4common/child_probe.h"
#include "proxy_probe.h"
#include "../../l4common/proxy_certificate.h"
typedef struct {WORD http,mqtt;const char* thumbprint;} CandidatePorts;
static bool candidate_check(HANDLE process,DWORD pid,ULONGLONG deadline,void* context) {
    CandidatePorts* ports=context;
    while(GetTickCount64()<deadline && WaitForSingleObject(process,0)==WAIT_TIMEOUT) {
        ULONGLONG now=GetTickCount64();if(now>=deadline)break;
        ULONGLONG left=deadline-now;if(left>1000)left=1000;
        if(l4_child_listeners(process,pid,ports->http,ports->mqtt) &&
           setup_proxy_probe_certificate(ports->http,(int)left,ports->thumbprint) &&
           l4_child_listeners(process,pid,ports->http,ports->mqtt))return true;
        if(WaitForSingleObject(process,50)!=WAIT_TIMEOUT)break;
    }
    SetLastError(ERROR_TIMEOUT);return false;
}
bool setup_update_candidate_probe(L4Journal* j,ULONGLONG sequence,unsigned hop,WORD http,WORD mqtt,DWORD timeout) {
    if(http<49152 || mqtt<49152 || http==mqtt || timeout<100 || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    SetupOperationPlan* p=NULL;if(!setup_update_load_operation(j,sequence,&p))return false;
    const SetupManifest* target=setup_prepared_manifest(p->packages,hop);
    unsigned count=0;const L4ReleaseFile* files=target?setup_manifest_files(target,&count):NULL;
    const L4ReleaseFile* exe=NULL;
    for(unsigned i=0;i<count;i++)if(!wcscmp(files[i].component,L"leo4proxy") && !wcscmp(files[i].file,L"leo4proxy.exe"))exe=&files[i];
    L4ReleaseFence* fence=NULL;wchar_t path[MAX_PATH],command[2048];bool ok=exe!=NULL;
    L4ProxyCertificate profile;
    L4ServiceToken* token=NULL;
    if(!ok)SetLastError(ERROR_INVALID_DATA);
    if(ok)ok=l4_proxy_certificate_source(p->switches[0][0].before.image_path,&profile);
    if(ok)ok=l4_release_pin(setup_manifest_layout(target),exe,&fence,path);
    if(ok)ok=l4_service_token_capture(&p->switches[0][0].before,&token);
    CandidatePorts ports={http,mqtt,ok?profile.thumbprint:NULL};
    if(ok)ok=l4_proxy_certificate_command(path,&profile,http,mqtt,timeout,command);
    if(ok)ok=l4_child_probe_service(token,path,setup_manifest_layout(target)->release,command,timeout,candidate_check,&ports);
    DWORD error=GetLastError();l4_service_token_close(token);l4_release_unpin(fence);setup_operation_free(p);return ok?true:fail(error?error:ERROR_INVALID_DATA);
}

struct SetupStopGate{
    L4Journal* journal;BYTE header[24];ULONGLONG sequence,generation;
    L4ReadinessSnapshot original;L4ServiceSwitch supervisor;volatile LONG spent;
};
void setup_update_stop_gate_free(SetupStopGate* gate){if(gate){SecureZeroMemory(gate,sizeof(*gate));free(gate);}}
static bool operation_source_plan(const SetupOperationPlan* p,L4BootstrapPlan* source){
    memset(source,0,sizeof(*source));if(!p->count)return fail(ERROR_NO_MORE_ITEMS);
    source->layout=*setup_manifest_layout(p->source);
    for(unsigned s=0;s<4;s++){const L4ServiceSwitch* first=&p->switches[0][s];
        wcscpy_s(source->services[s],32,first->service);wcscpy_s(source->commands[s],2048,first->before.image_path);
        source->start_types[s]=first->before.start_type;source->sizes[s]=first->before_size;memcpy(source->sha256[s],first->before_sha256,32);}
    return true;
}
static DWORD stop_left(ULONGLONG deadline){ULONGLONG now=GetTickCount64();return now<deadline?(DWORD)(deadline-now):0;}
bool setup_update_capture_stop(L4Journal* j,ULONGLONG sequence,const L4AccessActors* actors,
    const L4BootstrapChecks* checks,DWORD timeout,SetupStopGate** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!j || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE || !actors || !checks || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;L4UpdateState before,after;
    if(!l4_update_state_read(&j->layout,&before))return false;
    if(before.window || before.generation==~0ull)return fail(ERROR_BUSY);
    SetupOperationPlan* p=NULL;if(!setup_update_load_operation(j,sequence,&p))return false;
    SetupStopGate* gate=(SetupStopGate*)calloc(1,sizeof(*gate));L4BootstrapPlan source;
    bool ok=gate!=NULL;if(!ok)SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    if(ok)ok=operation_source_plan(p,&source);
    if(ok)gate->supervisor=p->switches[0][3];
    if(ok){DWORD remaining=stop_left(deadline);ok=remaining && setup_readiness_capture(j,&source,actors,p->configs,p->config_count,checks,remaining,&gate->original);}
    if(ok){ok=l4_update_state_read(&j->layout,&after);
        if(ok && (after.window || after.generation!=before.generation || after.plan_sequence!=before.plan_sequence ||
                  after.deadline_utc!=before.deadline_utc || memcmp(after.owner,before.owner,40)))ok=fail(ERROR_REVISION_MISMATCH);}
    if(ok && !stop_left(deadline))ok=false;
    DWORD error=stop_left(deadline)?GetLastError():ERROR_TIMEOUT;setup_operation_free(p);
    if(!ok){setup_update_stop_gate_free(gate);return fail(error?error:ERROR_REVISION_MISMATCH);}
    gate->journal=j;memcpy(gate->header,j->header,24);gate->sequence=sequence;gate->generation=before.generation;*result=gate;return true;
}
#include "../../l4common/probe_ipc.h"
static void stop_expected(L4Journal* j,const SetupStopGate* gate,ULONGLONG deadline,L4UpdateState* expected){
    memset(expected,0,sizeof(*expected));GUID guid;memcpy(&guid,j->header+8,16);
    snprintf(expected->owner,40,"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",guid.Data1,guid.Data2,guid.Data3,
        guid.Data4[0],guid.Data4[1],guid.Data4[2],guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7]);
    expected->window=L4_UPDATE_COMMUNICATION;expected->generation=gate->generation+1;
    expected->plan_sequence=gate->sequence;expected->deadline_utc=deadline;
}
bool setup_update_watch_ready(L4Journal* j,SetupStopGate* gate,ULONGLONG deadline_utc,DWORD timeout){
    if(!j || !gate || j!=gate->journal || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE ||
       memcmp(j->header,gate->header,24) || InterlockedCompareExchange(&gate->spent,0,0) ||
       !deadline_utc || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;L4UpdateState clear,expected,after;if(!l4_update_state_read(&j->layout,&clear))return false;
    if(clear.window || clear.generation!=gate->generation)return fail(ERROR_REVISION_MISMATCH);
    stop_expected(j,gate,deadline_utc,&expected);
    DWORD remaining=stop_left(deadline);
    if(!remaining)return fail(ERROR_TIMEOUT);
    if(!l4_probe_recovery_call(gate->original.services[3].pid,&expected,remaining))return false;
    if(!l4_update_state_read(&j->layout,&after))return false;
    if(memcmp(&clear,&after,sizeof(clear)))return fail(ERROR_REVISION_MISMATCH);
    return stop_left(deadline)?true:fail(ERROR_TIMEOUT);
}
bool setup_update_confirm_stop(L4Journal* j,SetupStopGate* gate,const L4AccessActors* actors,
    const L4BootstrapChecks* checks,DWORD timeout){
    if(!gate || InterlockedCompareExchange(&gate->spent,1,0))return fail(ERROR_INVALID_PARAMETER);
    if(!j || j!=gate->journal || j->poisoned || !j->lock || j->lock==INVALID_HANDLE_VALUE || memcmp(j->header,gate->header,24) ||
       !actors || !checks || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;L4UpdateState expected;
    if(!l4_update_state_read(&j->layout,&expected))return false;
    GUID guid;memcpy(&guid,j->header+8,16);char owner[40];
    snprintf(owner,40,"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",guid.Data1,guid.Data2,guid.Data3,
        guid.Data4[0],guid.Data4[1],guid.Data4[2],guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7]);
    if(expected.window!=L4_UPDATE_COMMUNICATION || expected.generation!=gate->generation+1 ||
       expected.plan_sequence!=gate->sequence || strcmp(expected.owner,owner))return fail(ERROR_REVISION_MISMATCH);
    DWORD recovery_left=stop_left(deadline);
    if(!recovery_left || !l4_probe_recovery_call(gate->original.services[3].pid,&expected,recovery_left))return false;
    SetupOperationPlan* p=NULL;if(!setup_update_load_operation(j,gate->sequence,&p))return false;L4BootstrapPlan source;
    bool ok=operation_source_plan(p,&source);
    if(ok){DWORD remaining=stop_left(deadline);ok=remaining && setup_readiness_quiescence(j,&source,actors,p->configs,p->config_count,checks,&gate->original,&expected,remaining);}
    if(ok){DWORD remaining=stop_left(deadline);ok=remaining && l4_probe_recovery_call(gate->original.services[3].pid,&expected,remaining);}
    if(ok && !stop_left(deadline))ok=false;
    DWORD error=stop_left(deadline)?GetLastError():ERROR_TIMEOUT;setup_operation_free(p);return ok?true:fail(error?error:ERROR_RETRY);
}

#include "../../l4common/worker_handoff.h"
bool setup_update_worker_capture_stop(L4Journal* j,ULONGLONG sequence,const L4AccessActors* actors,
    const L4BootstrapChecks* checks,DWORD timeout,SetupStopGate** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!j || !sequence || !actors || !checks || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;L4WorkerAdmission before={0},after={0};SetupStopGate* gate=NULL;
    bool ok=l4_worker_recheck(j,&before);if(ok && before.sequence!=sequence)ok=fail(ERROR_REVISION_MISMATCH);
    if(ok)ok=setup_update_capture_stop(j,sequence,actors,checks,stop_left(deadline),&gate);
    if(ok){const L4ServiceSwitch* supervisor=&gate->supervisor;
        ok=supervisor && before.generation==gate->generation && before.supervisor_pid==gate->original.services[3].pid &&
            !CompareFileTime(&before.supervisor_created,&gate->original.services[3].created) && before.start_type==supervisor->before.start_type &&
            !wcscmp(before.before,supervisor->before.image_path) && !wcscmp(before.after,supervisor->after) && before.old_size==supervisor->before_size && before.new_size==supervisor->size &&
            !memcmp(before.old_sha256,supervisor->before_sha256,32) && !memcmp(before.new_sha256,supervisor->sha256,32);
        if(!ok)SetLastError(ERROR_REVISION_MISMATCH);}
    if(ok)ok=l4_worker_recheck(j,&after);if(ok && memcmp(&before,&after,sizeof(before)))ok=fail(ERROR_REVISION_MISMATCH);
    if(ok && !stop_left(deadline))ok=fail(ERROR_TIMEOUT);DWORD error=GetLastError();
    if(!ok){setup_update_stop_gate_free(gate);return fail(error?error:ERROR_INVALID_STATE);}*result=gate;return true;
}

#include "../../l4common/recovery_store.h"
#include <objbase.h>
static bool communication_before_transfer(DWORD kind,ULONGLONG seq,const void* bytes,DWORD size,void* context){
    (void)seq;(void)bytes;(void)size;(void)context;
    return kind==68 || kind==69?fail(ERROR_INVALID_STATE):true;
}
static bool communication_supervisor(const L4RecoveryPlan* r,const SetupStopGate* gate){
    const L4ServiceSwitch* s=&gate->supervisor;
    return r->supervisor_pid==gate->original.services[3].pid && !CompareFileTime(&r->supervisor_created,&gate->original.services[3].created) &&
        r->start_type==s->before.start_type && !wcscmp(r->before,s->before.image_path) && !wcscmp(r->after,s->after) &&
        r->old_size==s->before_size && r->new_size==s->size && !memcmp(r->old_sha256,s->before_sha256,32) && !memcmp(r->new_sha256,s->sha256,32);
}
bool setup_update_prepare_communication(L4Journal* j,ULONGLONG sequence,L4WorkerJob* worker,
    const L4AccessActors* actors,const L4BootstrapChecks* checks,ULONGLONG armed_utc,
    ULONGLONG deadline_utc,const L4CommunicationBudget* budget,DWORD timeout){
    if(!j || !sequence || !worker || !actors || !checks || !armed_utc || deadline_utc<=armed_utc ||
       !l4_communication_native_budget_valid(budget) || !timeout || timeout>300000)return fail(ERROR_INVALID_PARAMETER);
    ULONGLONG deadline=GetTickCount64()+timeout;wchar_t operation[40];GUID id;memcpy(&id,j->header+8,16);
    wchar_t guid[40];if(StringFromGUID2(&id,guid,40)!=39)return fail(ERROR_INVALID_DATA);
    wcsncpy_s(operation,40,guid+1,36);
    if(!l4_journal_replay(j,communication_before_transfer,NULL))return false;
    L4RecoveryGuard* guard=NULL;DWORD remaining=stop_left(deadline);
    if(!remaining || !l4_recovery_open(&j->layout,operation,remaining,&guard))return false;
    const L4RecoveryPlan* recovery=l4_recovery_plan(guard);L4RecoveryAction action;
    ULONGLONG recovery_ticks=(ULONGLONG)budget->total_ms*10000;
    bool ok=recovery && recovery->sequence==sequence && deadline_utc<=~0ull-recovery_ticks &&
        deadline_utc+recovery_ticks<recovery->deadline_utc && l4_worker_job_verify(worker,recovery) &&
        l4_recovery_action(guard,armed_utc,false,&action) && action==L4_RECOVERY_WAIT;
    if(!ok)SetLastError(ERROR_REVISION_MISMATCH);l4_recovery_release(guard);
    SetupStopGate* gate=NULL;SetupOperationPlan* signed_plan=NULL;
    if(ok){remaining=stop_left(deadline);ok=remaining && setup_update_capture_stop(j,sequence,actors,checks,remaining,&gate);}
    if(ok && !communication_supervisor(recovery,gate))ok=fail(ERROR_REVISION_MISMATCH);
    if(ok)ok=setup_update_load_operation(j,sequence,&signed_plan);
    L4CommunicationPlan plan={0};BYTE *record=NULL,*switches[2]={0},*configs[2]={0},*con=NULL;DWORD size=0;
    if(ok)ok=l4_store_find_record(j,64,sequence,&record,&size);
    if(ok){memcpy(&plan.operation,j->header+8,16);plan.sequence=sequence;plan.generation=gate->generation+1;
        plan.armed_utc=armed_utc;plan.deadline_utc=deadline_utc;plan.worker_pid=recovery->worker_pid;plan.worker_created=recovery->worker_created;
        plan.supervisor_pid=recovery->supervisor_pid;plan.supervisor_created=recovery->supervisor_created;plan.budget=*budget;
        ok=l4_store_hash(record,size,NULL,0,plan.operation_sha256);}
    if(ok)for(unsigned i=0;ok && i<2;i++){
        plan.switch_sequence[i]=l4_store_get64(record+28+8*signed_plan->config_count+8*i);
        ok=l4_store_find_record(j,10,plan.switch_sequence[i],&switches[i],&plan.switch_size[i]);plan.switches[i]=switches[i];
    }
    if(ok){plan.con_sequence=l4_store_get64(record+28+8*signed_plan->config_count+16);
        ok=l4_store_find_record(j,10,plan.con_sequence,&con,&plan.con_size);plan.con_switch=con;
        plan.con_pid=gate->original.services[2].pid;plan.con_created=gate->original.services[2].created;
        L4BootstrapPlan source;remaining=stop_left(deadline);
        if(ok)ok=operation_source_plan(signed_plan,&source) && remaining && setup_readiness_proxy_identity(&source,&gate->original,remaining,plan.thumbprint);
    }
    const char* paths[]={"mosquitto\\mosquitto.conf","mosquitto\\acl.conf"};
    for(unsigned n=0;ok && n<signed_plan->config_count;n++){
        BYTE* config=NULL;DWORD length=0;ok=l4_store_find_record(j,20,signed_plan->configs[n],&config,&length);
        if(ok && length>=24){DWORD path_size=l4_store_get32(config+8);
            for(unsigned i=0;i<2;i++)if(path_size==strlen(paths[i]) && path_size<=length-24 && !memcmp(config+24,paths[i],path_size)){
                if(configs[i]){ok=fail(ERROR_DUP_NAME);break;}
                configs[i]=config;plan.configs[i]=config;plan.config_size[i]=length;plan.config_sequence[i]=signed_plan->configs[n];config=NULL;break;
            }}free(config);
    }
    if(ok && (!configs[0] || !configs[1]))ok=fail(ERROR_FILE_NOT_FOUND);
    if(ok)ok=l4_worker_job_verify(worker,recovery);
    if(ok){remaining=stop_left(deadline);ok=remaining && l4_recovery_relock(guard,remaining);}
    FILETIME now;GetSystemTimeAsFileTime(&now);ULONGLONG utc=((ULONGLONG)now.dwHighDateTime<<32)|now.dwLowDateTime;
    if(ok)ok=utc>=armed_utc && utc<deadline_utc && l4_recovery_action(guard,utc,false,&action) && action==L4_RECOVERY_WAIT && l4_worker_job_verify(worker,recovery);
    if(ok && stop_left(deadline))ok=l4_communication_plan_prepare(j,&plan,l4_worker_job_process(worker));
    else if(ok)ok=fail(ERROR_TIMEOUT);
    if(ok && !stop_left(deadline))ok=fail(ERROR_TIMEOUT);
    DWORD error=GetLastError();free(record);free(con);for(unsigned i=0;i<2;i++){free(switches[i]);free(configs[i]);}
    setup_operation_free(signed_plan);setup_update_stop_gate_free(gate);l4_recovery_close(guard);
    return ok?true:fail(error?error:ERROR_REVISION_MISMATCH);
}
