#include "../src/manifest.h"
#include "../../l4common/metadata.h"
#include <bcrypt.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "metadata_owner_fixture.h"
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL metadata %u: %s (error=%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static DWORD read_file(const wchar_t* directory,const wchar_t* name,BYTE* bytes,DWORD capacity){
    wchar_t path[MAX_PATH];swprintf_s(path,MAX_PATH,L"%ls\\%ls",directory,name);FILE* file=NULL;
    CHECK(_wfopen_s(&file,path,L"rb")==0 && file);if(!file)return 0;
    size_t size=fread(bytes,1,capacity,file);CHECK(!ferror(file) && feof(file));fclose(file);return (DWORD)size;
}
static bool zero(const BYTE* bytes){for(unsigned i=0;i<32;i++)if(bytes[i])return false;return true;}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 1;
    const char owner_probe[]="l4tools-metadata-owner-self-test-v1\n";BYTE owner_digest[32];
    CHECK(strlen(l4_metadata_key_id())==64);
    CHECK(l4_metadata_verify_trusted(owner_probe,(DWORD)strlen(owner_probe),owner_selftest_signature,sizeof(owner_selftest_signature),owner_digest));
    CHECK(!zero(owner_digest));
    CHECK(!l4_metadata_verify_trusted(owner_probe,(DWORD)strlen(owner_probe)-1,owner_selftest_signature,sizeof(owner_selftest_signature),owner_digest));CHECK(zero(owner_digest));
    BYTE public_key[412],wrong_key[412],signature[385],other[385],digest[32],document[65536];wchar_t name[MAX_PATH];
    DWORD public_size=read_file(argv[1],L"fixture-public.blob",public_key,sizeof(public_key));
    DWORD wrong_size=read_file(argv[1],L"fixture-wrong-public.blob",wrong_key,sizeof(wrong_key));
    swprintf_s(name,MAX_PATH,L"l4tools-layout-%ls.json",argv[2]);DWORD size=read_file(argv[1],name,document,sizeof(document));
    swprintf_s(name,MAX_PATH,L"l4tools-layout-%ls.json.sig",argv[2]);DWORD sig_size=read_file(argv[1],name,signature,sizeof(signature));
    CHECK(l4_metadata_verify(document,size,signature,sig_size,public_key,public_size,digest));CHECK(!zero(digest));
    BYTE expected[32];DWORD result_size=32;
    CHECK(CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM,0,NULL,document,size,expected,&result_size));CHECK(!memcmp(digest,expected,32));
    L4Layout layout;CHECK(l4_layout_from_roots(&layout,L"C:\\L4MetadataFixture\\Programs",L"C:\\L4MetadataFixture\\Data",L"1.13.3"));
    BYTE publisher[32];memset(publisher,0x11,32);char arch[8];CHECK(WideCharToMultiByte(CP_UTF8,0,argv[2],-1,arch,sizeof(arch),NULL,NULL));
    SetupManifest* manifest=NULL;CHECK(setup_manifest_parse_signed(document,size,signature,sig_size,public_key,public_size,publisher,&layout,arch,&manifest));
    CHECK(manifest);setup_manifest_free(manifest);manifest=NULL;
    /* Ephemeral fixture key cannot override the compiled owner trust root. */
    CHECK(!setup_manifest_parse_trusted(document,size,signature,sig_size,publisher,&layout,arch,&manifest));CHECK(!manifest);
    document[0]^=1;CHECK(!l4_metadata_verify(document,size,signature,sig_size,public_key,public_size,digest));CHECK(zero(digest));
    CHECK(!setup_manifest_parse_signed(document,size,signature,sig_size,public_key,public_size,publisher,&layout,arch,&manifest));CHECK(!manifest);document[0]^=1;
    signature[0]^=1;CHECK(!l4_metadata_verify(document,size,signature,sig_size,public_key,public_size,digest));CHECK(zero(digest));signature[0]^=1;
    CHECK(!l4_metadata_verify(document,size,signature,sig_size,wrong_key,wrong_size,digest));CHECK(zero(digest));
    CHECK(!l4_metadata_verify(document,size,signature,383,public_key,public_size,digest));CHECK(zero(digest));
    CHECK(!l4_metadata_verify(document,size,signature,385,public_key,public_size,digest));CHECK(!l4_metadata_verify(document,size-1,signature,sig_size,public_key,public_size,digest));
    CHECK(!l4_metadata_verify(NULL,size,signature,sig_size,public_key,public_size,digest));CHECK(!l4_metadata_verify(document,0,signature,sig_size,public_key,public_size,digest));
    CHECK(!l4_metadata_verify(document,65536,signature,sig_size,public_key,public_size,digest));CHECK(!l4_metadata_verify(document,size,NULL,sig_size,public_key,public_size,digest));
    CHECK(!l4_metadata_verify(document,size,signature,sig_size,NULL,public_size,digest));CHECK(!l4_metadata_verify(document,size,signature,sig_size,public_key,410,digest));
    CHECK(!l4_metadata_verify(document,size,signature,sig_size,public_key,public_size,NULL));
    const unsigned offsets[]={0,4,8,12,16,20,24,26,27,410};
    for(unsigned i=0;i<_countof(offsets);i++){memcpy(wrong_key,public_key,public_size);wrong_key[offsets[i]]^=0x80;
        CHECK(!l4_metadata_verify(document,size,signature,sig_size,wrong_key,public_size,digest));CHECK(zero(digest));}
    swprintf_s(name,MAX_PATH,L"%ls-pss.sig",argv[2]);DWORD other_size=read_file(argv[1],name,other,sizeof(other));
    CHECK(!l4_metadata_verify(document,size,other,other_size,public_key,public_size,digest));CHECK(zero(digest));
    swprintf_s(name,MAX_PATH,L"%ls-sha512.sig",argv[2]);other_size=read_file(argv[1],name,other,sizeof(other));
    CHECK(!l4_metadata_verify(document,size,other,other_size,public_key,public_size,digest));CHECK(zero(digest));
    size=read_file(argv[1],L"malformed.json",document,sizeof(document));sig_size=read_file(argv[1],L"malformed.json.sig",signature,sizeof(signature));
    CHECK(l4_metadata_verify(document,size,signature,sig_size,public_key,public_size,digest));
    CHECK(!setup_manifest_parse_signed(document,size,signature,sig_size,public_key,public_size,publisher,&layout,arch,&manifest));CHECK(!manifest);
    size=read_file(argv[1],L"maximum.bin",document,sizeof(document));sig_size=read_file(argv[1],L"maximum.bin.sig",signature,sizeof(signature));
    CHECK(size==65535);CHECK(l4_metadata_verify(document,size,signature,sig_size,public_key,public_size,digest));
    printf("Exact-byte Python/CNG metadata: %u checks, %u failures; no key/network/service writes\n",checks,failures);return failures?1:0;
}
