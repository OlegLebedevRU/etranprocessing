#include "manifest.h"
#include "../../l4common/metadata.h"
#include "../../leo4proxy/src/policy_json.h"
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>

#define FILE_LIMIT 64u
struct SetupManifest {
    L4Layout layout;char arch[8];BYTE archive_sha256[32],publisher_sha256[32];unsigned count;
    L4ReleaseFile files[FILE_LIMIT];wchar_t paths[FILE_LIMIT][MAX_PATH];
    unsigned configs[3];
};
static const char* executables[]={"leo4proxy/leo4proxy.exe","mosquitto/mosquitto.exe","l4con/l4con.exe",
    "l4superv/l4superv.exe","l4pin/l4pin.exe","l4desk/l4desk.exe","l4capture/bin/l4capture.exe",
    "ffmpeg/ffmpeg.exe","l4sql/l4sql.exe","l4launch/l4launch.exe"};
static const char* templates[]={"templates/l4superv.json","templates/mosquitto/acl.conf","templates/l4capture/idle_refresh.ini"};
static const wchar_t* config_targets[]={L"config\\l4superv.json",L"config\\mosquitto\\acl.conf",L"config\\l4capture\\idle_refresh.ini"};
static bool fail(DWORD error){SetLastError(error);return false;}
static bool nonzero(const BYTE* bytes){if(!bytes)return false;BYTE any=0;for(unsigned i=0;i<32;i++)any|=bytes[i];return any!=0;}
static bool hash(const void* bytes,DWORD size,const BYTE expected[32]){
    BYTE digest[32];BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE object=NULL;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0;
    if(ok)ok=BCryptCreateHash(algorithm,&object,NULL,0,NULL,0,0)>=0;
    if(ok)ok=BCryptHashData(object,(PUCHAR)bytes,size,0)>=0;
    if(ok)ok=BCryptFinishHash(object,digest,32,0)>=0 && !memcmp(digest,expected,32);
    if(object)BCryptDestroyHash(object);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static bool digest(const PolicyJson* json,int object,const char* key,BYTE out[32]){
    char text[65];if(!policy_json_string(json,policy_json_field(json,object,key),text,sizeof(text)) || strlen(text)!=64)return false;
    for(unsigned i=0;i<32;i++){int a=hex(text[i*2]),b=hex(text[i*2+1]);if(a<0||b<0)return false;out[i]=(BYTE)((a<<4)|b);}return nonzero(out);
}
static bool fields(const PolicyJson* json,int object,unsigned expected){
    if(object<0 || json->tokens[object].type!='{')return false;unsigned count=0;
    for(int i=object+1;i<json->tokens[object].next;i=json->tokens[i+1].next)++count;return count==expected;
}
static bool approved(const char* path,const wchar_t* component){
    if(!wcscmp(component,L"templates")){for(unsigned i=0;i<3;i++)if(!strcmp(path,templates[i]))return true;return false;}
    if(!wcscmp(component,L"suite"))return !strcmp(path,"suite/example_mosquitto.conf") || !strcmp(path,"suite/term_tool-user-guide.md");
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv",L"l4pin",L"l4desk",L"l4capture",L"ffmpeg",L"l4sql",L"l4launch",L"crt"};
    bool known=false;for(unsigned i=0;i<_countof(components);i++)known=known || !wcscmp(component,components[i]);if(!known)return false;
    for(const char* segment=path;;){const char* slash=strchr(segment,'/');if(!slash)break;size_t length=(size_t)(slash-segment);
        if((length==3 && !_strnicmp(segment,"log",3)) || (length==4 && !_strnicmp(segment,"logs",4)) ||
           (length==5 && (!_strnicmp(segment,"state",5) || !_strnicmp(segment,"cache",5))))return false;segment=slash+1;}
    const char* extension=strrchr(path,'.');
    if(extension && !strcmp(extension,".json"))return !strcmp(path,"l4capture/SBOM.json");
    if(extension && !strcmp(extension,".exe")){for(unsigned i=0;i<_countof(executables);i++)if(!strcmp(path,executables[i]))return true;return false;}
    return (extension && (!strcmp(extension,".md") || !strcmp(extension,".txt") || !strcmp(extension,".crt"))) || !strcmp(strrchr(path,'/')+1,"LICENSE");
}
void setup_manifest_free(SetupManifest* manifest){free(manifest);}
const BYTE* setup_manifest_archive_sha256(const SetupManifest* manifest){return manifest?manifest->archive_sha256:NULL;}
bool setup_manifest_parse_trusted(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE publisher_sha256[32],const L4Layout* layout,const char* arch,SetupManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;BYTE authenticated[32];
    return l4_metadata_verify_trusted(bytes,size,signature,signature_size,authenticated) &&
        setup_manifest_parse(bytes,size,authenticated,publisher_sha256,layout,arch,result);
}
bool setup_manifest_parse_signed(const void* bytes,DWORD size,const BYTE* signature,DWORD signature_size,
    const BYTE* trusted_public,DWORD public_size,const BYTE publisher_sha256[32],
    const L4Layout* layout,const char* arch,SetupManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;BYTE authenticated[32];
    return l4_metadata_verify(bytes,size,signature,signature_size,trusted_public,public_size,authenticated) &&
        setup_manifest_parse(bytes,size,authenticated,publisher_sha256,layout,arch,result);
}
bool setup_manifest_parse(const void* bytes,DWORD size,const BYTE descriptor_sha256[32],const BYTE publisher_sha256[32],const L4Layout* layout,const char* arch,SetupManifest** result){
    if(!result)return fail(ERROR_INVALID_PARAMETER);*result=NULL;
    if(!bytes || !size || size>65535 || !layout || !arch || (strcmp(arch,"x86") && strcmp(arch,"x64")) || !nonzero(descriptor_sha256) || !nonzero(publisher_sha256))return fail(ERROR_INVALID_PARAMETER);
    if(!hash(bytes,size,descriptor_sha256))return fail(ERROR_CRC); /* Authenticate before interpreting any paths. */
    PolicyJson json;unsigned long long schema;char version[64],architecture[8];BYTE archive[32],publisher[32];L4Layout validated;
    if(!policy_json_parse(&json,(const char*)bytes,size) || !fields(&json,0,6) ||
       !policy_json_uint(&json,policy_json_field(&json,0,"schema"),&schema) || schema!=1 ||
       !policy_json_string(&json,policy_json_field(&json,0,"version"),version,sizeof(version)) ||
       !policy_json_string(&json,policy_json_field(&json,0,"arch"),architecture,sizeof(architecture)) || strcmp(arch,architecture) ||
       !digest(&json,0,"archive_sha256",archive) || !digest(&json,0,"publisher_certificate_sha256",publisher) || memcmp(publisher,publisher_sha256,32))return fail(ERROR_INVALID_DATA);
    wchar_t wide_version[64];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,version,-1,wide_version,64) ||
       !l4_layout_from_roots(&validated,layout->binaries,layout->data,wide_version) || memcmp(&validated,layout,sizeof(validated)))return fail(ERROR_REVISION_MISMATCH);
    int array=policy_json_field(&json,0,"files");if(array<0 || json.tokens[array].type!='[')return fail(ERROR_INVALID_DATA);
    SetupManifest* manifest=(SetupManifest*)calloc(1,sizeof(*manifest));if(!manifest)return fail(ERROR_NOT_ENOUGH_MEMORY);
    manifest->layout=*layout;strcpy_s(manifest->arch,sizeof(manifest->arch),architecture);memcpy(manifest->archive_sha256,archive,32);memcpy(manifest->publisher_sha256,publisher,32);
    unsigned exe_mask=0,config_mask=0;ULONGLONG total=0;bool ok=true;
    for(int n=array+1;n<json.tokens[array].next && ok;n=json.tokens[n].next){
        char path[MAX_PATH];unsigned long long file_size=0;unsigned index=manifest->count;
        ok=index<FILE_LIMIT && fields(&json,n,3) && policy_json_string(&json,policy_json_field(&json,n,"path"),path,sizeof(path)) &&
            policy_json_uint(&json,policy_json_field(&json,n,"size"),&file_size) && file_size<=512ULL*1024*1024;
        if(!ok)break;
        total+=file_size;if(total>1024ULL*1024*1024){ok=false;break;}
        /* Canonical ASCII archive paths; no Windows aliases, root assets or mutable data. */
        for(char* p=path;*p;p++)if(!((*p>='a'&&*p<='z') || (*p>='A'&&*p<='Z') || (*p>='0'&&*p<='9') || strchr("/._-",*p))){ok=false;break;}
        wchar_t* stored=manifest->paths[index];wchar_t checked[MAX_PATH];
        if(ok)ok=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,stored,MAX_PATH)!=0;
        wchar_t* slash=ok?wcschr(stored,L'/'):NULL;if(!slash){ok=false;break;}*slash=0;
        L4ReleaseFile* file=&manifest->files[index];file->component=stored;file->file=slash+1;file->size=file_size;
        for(wchar_t* p=slash+1;*p;p++)if(*p==L'/')*p=L'\\';
        ok=ok && approved(path,file->component) && l4_layout_component(layout,file->component,file->file,checked) && digest(&json,n,"sha256",file->sha256);
        for(unsigned j=0;ok && j<index;j++){wchar_t prior[MAX_PATH];ok=l4_layout_component(layout,manifest->files[j].component,manifest->files[j].file,prior) && _wcsicmp(checked,prior)!=0;}
        for(unsigned j=0;j<_countof(executables);j++)if(!strcmp(path,executables[j]))exe_mask|=1u<<j;
        for(unsigned j=0;j<3;j++)if(!strcmp(path,templates[j])){config_mask|=1u<<j;manifest->configs[j]=index;}
        if(ok)++manifest->count;
    }
    if(!ok || exe_mask!=((1u<<_countof(executables))-1) || config_mask!=7){setup_manifest_free(manifest);return fail(ERROR_INVALID_DATA);}
    *result=manifest;return true;
}
const L4ReleaseFile* setup_manifest_files(const SetupManifest* manifest,unsigned* count){if(!manifest || !count){fail(ERROR_INVALID_PARAMETER);return NULL;}*count=manifest->count;return manifest->files;}
const L4Layout* setup_manifest_layout(const SetupManifest* manifest){return manifest?&manifest->layout:NULL;}
bool setup_manifest_prepare(const SetupManifest* manifest,const wchar_t* archive){
    if(!manifest)return fail(ERROR_INVALID_PARAMETER);
    return setup_release_prepare(&manifest->layout,archive,manifest->archive_sha256,manifest->files,manifest->count,manifest->publisher_sha256);
}
bool setup_manifest_verify(const SetupManifest* manifest){
    if(!manifest)return fail(ERROR_INVALID_PARAMETER);
    return setup_release_verify(&manifest->layout,manifest->files,manifest->count,manifest->publisher_sha256);
}
bool setup_manifest_config(const SetupManifest* manifest,unsigned index,wchar_t source[MAX_PATH],wchar_t destination[MAX_PATH]){
    if(!manifest || index>=3 || !source || !destination)return fail(ERROR_INVALID_PARAMETER);
    const L4ReleaseFile* file=&manifest->files[manifest->configs[index]];
    return l4_layout_component(&manifest->layout,file->component,file->file,source) && l4_layout_data_path(&manifest->layout,config_targets[index],destination);
}

bool setup_manifest_prepare_policy(const SetupManifest* m,const wchar_t* archive,SetupAdmissionPolicy policy){
    if(!m || !archive)return fail(ERROR_INVALID_PARAMETER);
    return setup_release_prepare_policy(&m->layout,archive,m->archive_sha256,m->files,m->count,m->publisher_sha256,policy);
}
bool setup_manifest_verify_policy(const SetupManifest* m,SetupAdmissionPolicy policy){
    if(!m)return fail(ERROR_INVALID_PARAMETER);
    return setup_release_verify_policy(&m->layout,m->files,m->count,m->publisher_sha256,policy);
}

const char* setup_manifest_arch(const SetupManifest* m){return m?m->arch:NULL;}
