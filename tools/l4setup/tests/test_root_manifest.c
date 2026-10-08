#include "../src/root_manifest.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL root %u: %s (error=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static DWORD read_file(const wchar_t* directory,const wchar_t* name,BYTE* bytes,DWORD capacity){
    wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls\\%ls",directory,name);FILE* file=NULL;
    CHECK(_wfopen_s(&file,path,L"rb")==0 && file);if(!file)return 0;
    size_t size=fread(bytes,1,capacity,file);CHECK(!ferror(file) && feof(file));fclose(file);return (DWORD)size;
}
static void digest(const void* bytes,DWORD size,BYTE result[32]){DWORD length=32;CHECK(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,bytes,size,result,&length));}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 1;BYTE doc[65536],sig[385],public_key[412],layout_doc[65536],layout_sig[385],text[128];wchar_t name[80];
    DWORD public_size=read_file(argv[1],L"fixture-public.blob",public_key,sizeof(public_key));
    DWORD n=read_file(argv[1],L"fixture-key-id.txt",text,sizeof(text)-1);text[n]=0;char key_id[65];strcpy_s(key_id,sizeof(key_id),(char*)text);
    char arch[8];CHECK(WideCharToMultiByte(CP_UTF8,0,argv[2],-1,arch,sizeof(arch),NULL,NULL));
    DWORD size=read_file(argv[1],L"l4tools-release.json",doc,sizeof(doc));
    DWORD sig_size=read_file(argv[1],L"l4tools-release.json.sig",sig,sizeof(sig));
    L4CatalogRelease release={0};strcpy_s(release.version,sizeof(release.version),"1.13.3");digest(doc,size,release.manifest_sha256);
    L4Layout layout;CHECK(l4_layout_from_roots(&layout,L"C:\\L4RootTest\\Programs",L"C:\\L4RootTest\\Data",L"1.13.3"));
    SetupRootManifest* root=NULL;SetupManifest* manifest=NULL;
    CHECK(!setup_root_parse_trusted(doc,size,sig,sig_size,&release,arch,&root));CHECK(!root);
    CHECK(setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));CHECK(root);
    CHECK(setup_root_matches(root,&release,arch));CHECK(!setup_root_matches(root,&release,"arm64"));
    release.manifest_sha256[0]^=1;CHECK(!setup_root_matches(root,&release,arch));release.manifest_sha256[0]^=1;
    release.revoked=true;CHECK(!setup_root_matches(root,&release,arch));release.revoked=false;
    for(unsigned i=0;i<3;i++)CHECK(setup_root_asset(root,i) && setup_root_asset(root,i)->size);
    CHECK(!setup_root_asset(root,3));CHECK(!setup_root_asset(NULL,0));
    swprintf_s(name,_countof(name),L"l4tools-layout-%ls.json",argv[2]);DWORD layout_size=read_file(argv[1],name,layout_doc,sizeof(layout_doc));
    swprintf_s(name,_countof(name),L"l4tools-layout-%ls.json.sig",argv[2]);DWORD layout_sig_size=read_file(argv[1],name,layout_sig,sizeof(layout_sig));
    CHECK(setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,layout_sig_size,public_key,public_size,&layout,&manifest));CHECK(manifest);
    CHECK(!memcmp(setup_manifest_archive_sha256(manifest),setup_root_asset(root,2)->sha256,32));setup_manifest_free(manifest);manifest=NULL;
    CHECK(!setup_root_descriptor_trusted(root,layout_doc,layout_size,layout_sig,layout_sig_size,&layout,&manifest));CHECK(!manifest);
    CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size-1,layout_sig,layout_sig_size,public_key,public_size,&layout,&manifest));CHECK(!manifest);
    CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,383,public_key,public_size,&layout,&manifest));
    layout_doc[0]^=1;CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,layout_sig_size,public_key,public_size,&layout,&manifest));layout_doc[0]^=1;
    layout_sig[0]^=1;CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,layout_sig_size,public_key,public_size,&layout,&manifest));layout_sig[0]^=1;
    public_key[410]^=2;CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,layout_sig_size,public_key,public_size,&layout,&manifest));public_key[410]^=2;
    L4Layout old;CHECK(l4_layout_from_roots(&old,layout.binaries,layout.data,L"1.13.2"));
    CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,layout_sig_size,public_key,public_size,&old,&manifest));
    CHECK(!setup_root_descriptor_signed(root,NULL,layout_size,layout_sig,layout_sig_size,public_key,public_size,&layout,&manifest));
    CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,layout_sig_size,public_key,public_size,NULL,&manifest));
    CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,layout_sig_size,public_key,public_size,&layout,NULL));
    setup_root_free(root);root=NULL;
    doc[0]^=1;CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));CHECK(!root);doc[0]^=1;
    sig[0]^=1;CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));sig[0]^=1;
    release.manifest_sha256[0]^=1;CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));CHECK(!root);release.manifest_sha256[0]^=1;
    release.revoked=true;CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));release.revoked=false;
    strcpy_s(release.version,sizeof(release.version),"1.13.2");CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));strcpy_s(release.version,sizeof(release.version),"1.13.3");
    CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,"arm64",&root));
    CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,NULL,&release,arch,&root));
    CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,NULL,arch,&root));
    CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,NULL));
    n=read_file(argv[1],L"bad-count.txt",text,sizeof(text)-1);text[n]=0;unsigned count=(unsigned)strtoul((char*)text,NULL,10);CHECK(count==36);
    for(unsigned i=0;i<count;i++){
        swprintf_s(name,_countof(name),L"bad-%u.json",i);size=read_file(argv[1],name,doc,sizeof(doc));
        swprintf_s(name,_countof(name),L"bad-%u.sig",i);sig_size=read_file(argv[1],name,sig,sizeof(sig));digest(doc,size,release.manifest_sha256);
        /* Prove signature-valid malformed metadata is refused after authentication. */
        BYTE hash[32];CHECK(l4_metadata_verify(doc,size,sig,sig_size,public_key,public_size,hash));
        CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));CHECK(!root);
    }
    size=read_file(argv[1],L"duplicate.json",doc,sizeof(doc));sig_size=read_file(argv[1],L"duplicate.sig",sig,sizeof(sig));digest(doc,size,release.manifest_sha256);
    CHECK(!setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));CHECK(!root);
    const wchar_t* variants[]={L"archive",L"publisher"};
    for(unsigned i=0;i<2;i++){
        swprintf_s(name,_countof(name),L"mismatch-%ls.json",variants[i]);size=read_file(argv[1],name,doc,sizeof(doc));
        swprintf_s(name,_countof(name),L"mismatch-%ls.sig",variants[i]);sig_size=read_file(argv[1],name,sig,sizeof(sig));digest(doc,size,release.manifest_sha256);
        CHECK(setup_root_parse_signed(doc,size,sig,sig_size,public_key,public_size,key_id,&release,arch,&root));
        CHECK(!setup_root_descriptor_signed(root,layout_doc,layout_size,layout_sig,layout_sig_size,public_key,public_size,&layout,&manifest));CHECK(!manifest);setup_root_free(root);root=NULL;
    }
    CHECK(!setup_manifest_archive_sha256(NULL));
    printf("Catalog/root/descriptor native admission: %u checks, %u failures; no network/service/file writes\n",checks,failures);return failures?1:0;
}
