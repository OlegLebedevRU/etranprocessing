#include "root_manifest.h"
#include "../../leo4proxy/src/policy_json.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct SetupRootManifest {
    char version[64],arch[8];BYTE publisher[32],public_key[L4_METADATA_PUBLIC_BYTES],manifest_sha256[32];
    bool owner_trusted;SetupRootAsset assets[3],installer;
};
static bool fail(DWORD code){SetLastError(code);return false;}
static bool nonzero(const BYTE* value){BYTE any=0;for(unsigned i=0;i<32;i++)any|=value[i];return any!=0;}
static bool hash_matches(const void* bytes,DWORD size,const BYTE expected[32]){
    if(!bytes)return false;BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE h=NULL;BYTE actual[32];
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)==0;
    if(ok)ok=BCryptCreateHash(alg,&h,NULL,0,NULL,0,0)==0;
    if(ok)ok=BCryptHashData(h,(PUCHAR)bytes,size,0)==0;
    if(ok)ok=BCryptFinishHash(h,actual,32,0)==0 && !memcmp(actual,expected,32);
    if(h)BCryptDestroyHash(h);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
static bool fields(const PolicyJson* j,int n,unsigned count){
    if(n<0 || j->tokens[n].type!='{')return false;unsigned actual=0;
    for(int i=n+1;i<j->tokens[n].next;i=j->tokens[i+1].next)++actual;return actual==count;
}
static bool text(const PolicyJson* j,int n,const char* key,char* out,size_t size){return policy_json_string(j,policy_json_field(j,n,key),out,size);}
static bool equal(const PolicyJson* j,int n,const char* key,const char* expected){char value[128];return text(j,n,key,value,sizeof(value)) && !strcmp(value,expected);}
static bool number(const PolicyJson* j,int n,const char* key,ULONGLONG* out){return policy_json_uint(j,policy_json_field(j,n,key),out);}
static bool boolean(const PolicyJson* j,int n,const char* key,bool expected){bool value;return policy_json_bool(j,policy_json_field(j,n,key),&value) && value==expected;}
static bool digest(const PolicyJson* j,int n,const char* key,BYTE out[32]){
    char value[65];if(!text(j,n,key,value,sizeof(value)) || strlen(value)!=64)return false;
    for(unsigned i=0;i<32;i++){unsigned a=(unsigned char)value[2*i],b=(unsigned char)value[2*i+1];
        a=a>='0'&&a<='9'?a-'0':a>='a'&&a<='f'?a-'a'+10:16;
        b=b>='0'&&b<='9'?b-'0':b>='a'&&b<='f'?b-'a'+10:16;
        if(a>15 || b>15)return false;out[i]=(BYTE)((a<<4)|b);}
    return nonzero(out);
}
static bool asset(const PolicyJson* j,int files,const char* name,ULONGLONG limit,SetupRootAsset* out){
    int n=policy_json_field(j,files,name);strcpy_s(out->name,sizeof(out->name),name);
    return fields(j,n,2) && number(j,n,"size",&out->size) && out->size && out->size<=limit && digest(j,n,"sha256",out->sha256);
}
void setup_root_free(SetupRootManifest* root){free(root);}
bool setup_root_matches(const SetupRootManifest* root,const L4CatalogRelease* release,const char* arch){
    return root && release && arch && !release->revoked && memchr(release->version,0,sizeof(release->version)) &&
        !strcmp(root->version,release->version) && !strcmp(root->arch,arch) && !memcmp(root->manifest_sha256,release->manifest_sha256,32);
}
const SetupRootAsset* setup_root_asset(const SetupRootManifest* root,unsigned index){return root && index<3?&root->assets[index]:NULL;}
const SetupRootAsset* setup_root_installer(const SetupRootManifest* root){return root && root->owner_trusted?&root->installer:NULL;}
const BYTE* setup_root_publisher(const SetupRootManifest* root){return root && root->owner_trusted?root->publisher:NULL;}
const BYTE* setup_root_identity(const SetupRootManifest* root){return root && root->owner_trusted?root->manifest_sha256:NULL;}
static bool parse(const void* bytes,DWORD size,const BYTE authenticated[32],const char* key_id,
    const L4CatalogRelease* release,const char* arch,SetupRootManifest** result){
    if(!release || !memchr(release->version,0,sizeof(release->version)) || release->revoked || !arch || (strcmp(arch,"x86") && strcmp(arch,"x64")) || !key_id ||
       !nonzero(release->manifest_sha256))return fail(ERROR_INVALID_PARAMETER);
    if(memcmp(authenticated,release->manifest_sha256,32))return fail(ERROR_CRC);
    PolicyJson j;ULONGLONG schema;BYTE unused[32];char version[64];L4Layout checked;wchar_t wide[64];
    const char* keys[]={"schema","version","git_sha","dirty","built_at","builder","signed","signature_status","files",
        "payload_sha256","components","component_artifacts","min_os","arch","source_checkpoint",
        "publisher_certificate_sha256","layout_payloads","metadata_signatures"};
    if(!policy_json_parse(&j,(const char*)bytes,size) || !fields(&j,0,_countof(keys)))return fail(ERROR_INVALID_DATA);
    for(unsigned i=0;i<_countof(keys);i++)if(policy_json_field(&j,0,keys[i])<0)return fail(ERROR_INVALID_DATA);
    if(!number(&j,0,"schema",&schema) || schema!=1 || !text(&j,0,"version",version,sizeof(version)) ||
       strcmp(version,release->version) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide,64) ||
       !l4_layout_from_roots(&checked,L"C:\\L4RootFixture\\Programs",L"C:\\L4RootFixture\\Data",wide) ||
       !boolean(&j,0,"dirty",false) || !boolean(&j,0,"signed",true) || !equal(&j,0,"signature_status","Valid") ||
       !equal(&j,0,"min_os","6.1"))return fail(ERROR_INVALID_DATA);
    int signature=policy_json_field(&j,0,"metadata_signatures"),checkpoint=policy_json_field(&j,0,"source_checkpoint");
    if(!fields(&j,signature,4) || !number(&j,signature,"schema",&schema) || schema!=1 ||
       !equal(&j,signature,"algorithm","RSA3072-PKCS1v1.5-SHA256") || !digest(&j,signature,"key_id",unused) ||
       !equal(&j,signature,"key_id",key_id) || !equal(&j,signature,"signature","l4tools-release.json.sig") ||
       !fields(&j,checkpoint,4) || !number(&j,checkpoint,"schema",&schema) || schema!=1 ||
       !boolean(&j,checkpoint,"clean_at_start",true) || !digest(&j,checkpoint,"inputs_sha256",unused) ||
       !digest(&j,checkpoint,"build_options_sha256",unused))return fail(ERROR_INVALID_DATA);
    int architectures=policy_json_field(&j,0,"arch");
    if(architectures<0 || j.tokens[architectures].type!='[')return fail(ERROR_INVALID_DATA);
    unsigned mask=0,count=0;
    for(int i=architectures+1;i<j.tokens[architectures].next;i=j.tokens[i].next){char a[8];
        if(!policy_json_string(&j,i,a,sizeof(a)))return fail(ERROR_INVALID_DATA);
        unsigned bit=!strcmp(a,"x86")?1u:!strcmp(a,"x64")?2u:0;
        if(!bit || (mask&bit))return fail(ERROR_INVALID_DATA);mask|=bit;++count;}
    if(mask!=3 || count!=2)return fail(ERROR_INVALID_DATA);
    int files=policy_json_field(&j,0,"files"),layouts=policy_json_field(&j,0,"layout_payloads");
    if(!fields(&j,files,7) || !fields(&j,layouts,2))return fail(ERROR_INVALID_DATA);
    SetupRootManifest* root=(SetupRootManifest*)calloc(1,sizeof(*root));if(!root)return fail(ERROR_NOT_ENOUGH_MEMORY);
    strcpy_s(root->version,sizeof(root->version),version);strcpy_s(root->arch,sizeof(root->arch),arch);
    memcpy(root->manifest_sha256,authenticated,32);
    SetupRootAsset setup={0};bool ok=digest(&j,0,"publisher_certificate_sha256",root->publisher) && asset(&j,files,"l4setup.exe",1024ULL*1024*1024,&setup);
    /* Validate both inventories, retain only selected architecture. Names are fixed;
     * metadata cannot nominate an authority, path, redirect, optional missing file. */
    const char* arches[]={"x86","x64"};
    for(unsigned a=0;ok && a<2;a++){
        int entry=policy_json_field(&j,layouts,arches[a]);SetupRootAsset items[3];char name[40];BYTE expected[32];
        ok=fields(&j,entry,4);
        sprintf_s(name,sizeof(name),"l4tools-layout-%s.json",arches[a]);
        ok=ok && equal(&j,entry,"manifest",name) && asset(&j,files,name,L4_METADATA_MAX_BYTES,&items[0]) &&
            digest(&j,entry,"manifest_sha256",expected) && !memcmp(expected,items[0].sha256,32);
        sprintf_s(name,sizeof(name),"l4tools-layout-%s.json.sig",arches[a]);
        ok=ok && asset(&j,files,name,L4_METADATA_SIGNATURE_BYTES,&items[1]) && items[1].size==L4_METADATA_SIGNATURE_BYTES;
        sprintf_s(name,sizeof(name),"l4tools-layout-%s.zip",arches[a]);
        ok=ok && equal(&j,entry,"archive",name) && asset(&j,files,name,1024ULL*1024*1024,&items[2]) &&
            digest(&j,entry,"archive_sha256",expected) && !memcmp(expected,items[2].sha256,32);
        if(ok && !strcmp(arch,arches[a]))memcpy(root->assets,items,sizeof(items));
    }
    if(!ok){setup_root_free(root);return fail(ERROR_INVALID_DATA);}root->installer=setup;*result=root;return true;
}
bool setup_root_parse_trusted(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const L4CatalogRelease* release,const char* arch,SetupRootManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;BYTE hash[32];
    bool ok=l4_metadata_verify_trusted(bytes,size,signature,signature_size,hash) && parse(bytes,size,hash,l4_metadata_key_id(),release,arch,result);
    if(ok)(*result)->owner_trusted=true;return ok;
}
bool setup_root_parse_signed(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* public_key,DWORD public_size,const char* key_id,const L4CatalogRelease* release,const char* arch,SetupRootManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;BYTE hash[32];
    bool ok=l4_metadata_verify(bytes,size,signature,signature_size,public_key,public_size,hash) && parse(bytes,size,hash,key_id,release,arch,result);
    if(ok)memcpy((*result)->public_key,public_key,public_size);return ok;
}
static bool descriptor(const SetupRootManifest* root,const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* public_key,DWORD public_size,const L4Layout* layout,SetupManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!root || !layout || !bytes || !signature)return fail(ERROR_INVALID_PARAMETER);
    const wchar_t* tail=wcsrchr(layout->release,L'\\');char version[64];
    if(!tail || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,tail+1,-1,version,sizeof(version),NULL,NULL) || strcmp(version,root->version))return fail(ERROR_REVISION_MISMATCH);
    if(size!=root->assets[0].size || signature_size!=root->assets[1].size ||
       !hash_matches(bytes,size,root->assets[0].sha256) || !hash_matches(signature,signature_size,root->assets[1].sha256))return fail(ERROR_CRC);
    bool ok=root->owner_trusted?setup_manifest_parse_trusted(bytes,size,signature,signature_size,root->publisher,layout,root->arch,result):
        setup_manifest_parse_signed(bytes,size,signature,signature_size,public_key,public_size,root->publisher,layout,root->arch,result);
    if(ok && memcmp(setup_manifest_archive_sha256(*result),root->assets[2].sha256,32)){
        setup_manifest_free(*result);*result=NULL;return fail(ERROR_CRC);}
    return ok;
}
bool setup_root_descriptor_trusted(const SetupRootManifest* root,const void* bytes,DWORD size,
    const BYTE* signature,DWORD signature_size,const L4Layout* layout,SetupManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!root || !root->owner_trusted)return fail(ERROR_ACCESS_DENIED);
    return descriptor(root,bytes,size,signature,signature_size,NULL,0,layout,result);
}
bool setup_root_descriptor_signed(const SetupRootManifest* root,const void* bytes,DWORD size,
    const BYTE* signature,DWORD signature_size,const BYTE* public_key,DWORD public_size,const L4Layout* layout,SetupManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!root || root->owner_trusted || !public_key || public_size!=L4_METADATA_PUBLIC_BYTES || memcmp(root->public_key,public_key,public_size))return fail(ERROR_ACCESS_DENIED);
    return descriptor(root,bytes,size,signature,signature_size,public_key,public_size,layout,result);
}
