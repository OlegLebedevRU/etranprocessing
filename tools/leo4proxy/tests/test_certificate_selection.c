#include "cert_store.h"
#include <ncrypt.h>
#include <stdio.h>
static const wchar_t* key_name = L"Leo4ProxySelectionFixture";
static PCCERT_CONTEXT make_cert(NCRYPT_KEY_HANDLE key, const char* dn, int start, int duration) {
    BYTE name[1024]; DWORD count=sizeof(name);
    if (!CertStrToNameA(X509_ASN_ENCODING,dn,CERT_X500_NAME_STR,NULL,name,&count,NULL)) return NULL;
    CERT_NAME_BLOB blob={count,name};
    FILETIME now; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER value; value.LowPart=now.dwLowDateTime; value.HighPart=now.dwHighDateTime;
    value.QuadPart+=(LONGLONG)start*864000000000LL;
    FILETIME begin={value.LowPart,value.HighPart}; SYSTEMTIME a,b; FileTimeToSystemTime(&begin,&a);
    value.QuadPart+=(LONGLONG)duration*864000000000LL;
    FILETIME end={value.LowPart,value.HighPart}; FileTimeToSystemTime(&end,&b);
    CRYPT_KEY_PROV_INFO info={0}; info.pwszContainerName=(LPWSTR)key_name;
    info.pwszProvName=(LPWSTR)MS_KEY_STORAGE_PROVIDER; info.dwKeySpec=0;
    return CertCreateSelfSignCertificate((HCRYPTPROV_OR_NCRYPT_KEY_HANDLE)key,&blob,0,&info,NULL,&a,&b,NULL);
}
/* Configuration fixture; production selection is linked unchanged. */
void proxy_config_init_defaults(ProxyConfig* config) { memset(config,0,sizeof(*config)); }
static int failed;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d\n",__LINE__); failed++; } } while(0)
int main(void) {
    NCRYPT_PROV_HANDLE provider=0; NCRYPT_KEY_HANDLE key=0;
    if (NCryptOpenStorageProvider(&provider,MS_KEY_STORAGE_PROVIDER,0)) return 1;
    if (NCryptCreatePersistedKey(provider,&key,BCRYPT_RSA_ALGORITHM,key_name,0,0)) { NCryptFreeObject(provider); return 1; }
    DWORD length=2048; NCryptSetProperty(key,NCRYPT_LENGTH_PROPERTY,(PBYTE)&length,sizeof(length),0);
    if (NCryptFinalizeKey(key,0)) { NCryptDeleteKey(key,0); NCryptFreeObject(provider); return 1; }
    HCERTSTORE store=CertOpenStore(CERT_STORE_PROV_MEMORY,0,0,0,NULL);
    ProxyConfig config; proxy_config_init_defaults(&config); CertDetails details;
    PCCERT_CONTEXT legacy=make_cert(key,"CN=certsrv,E=1.terminal@forpay.ru",-1,366);
    PCCERT_CONTEXT wrong=make_cert(key,"CN=prefix.iot.leo4.ru,E=773.terminal@leo4.ru",-1,366);
    CHECK(legacy && wrong);
    CertAddCertificateContextToStore(store,legacy,CERT_STORE_ADD_ALWAYS,NULL);
    CHECK(!cert_store_find_in_store(store,&config,&details));
    DWORD thumb_size=20; BYTE thumb[20]; CertGetCertificateContextProperty(legacy,CERT_SHA1_HASH_PROP_ID,thumb,&thumb_size);
    for(int i=0;i<20;i++) sprintf_s(config.cert_thumbprint+i*2,sizeof(config.cert_thumbprint)-i*2,"%02X",thumb[i]);
    CHECK(!cert_store_find_in_store(store,&config,&details)); config.cert_thumbprint[0]=0;
    CertAddCertificateContextToStore(store,wrong,CERT_STORE_ADD_ALWAYS,NULL);
    CHECK(!cert_store_find_in_store(store,&config,&details));
    PCCERT_CONTEXT expired=make_cert(key,"CN=iot.leo4.ru,E=old.terminal@leo4.ru",-10,2);
    PCCERT_CONTEXT future=make_cert(key,"CN=iot.leo4.ru,E=future.terminal@leo4.ru",10,365);
    PCCERT_CONTEXT old=make_cert(key,"CN=iot.leo4.ru,E=old.terminal@leo4.ru",-1,90);
    PCCERT_CONTEXT fresh=make_cert(key,"CN=iot.leo4.ru,E=fresh.terminal@leo4.ru",-2,366);
    CHECK(expired && future && old && fresh);
    PCCERT_CONTEXT items[]={expired,future,old,fresh};
    for(int i=0;i<4;i++) CertAddCertificateContextToStore(store,items[i],CERT_STORE_ADD_ALWAYS,NULL);
    CHECK(cert_store_find_in_store(store,&config,&details));
    CHECK(!strcmp(details.email,"fresh.terminal@leo4.ru")); cert_store_free_details(&details);
    config.drop_on_expire=0;
    CHECK(cert_store_find_in_store(store,&config,&details));
    CHECK(!strcmp(details.email,"fresh.terminal@leo4.ru")); cert_store_free_details(&details);
    for(int i=0;i<4;i++) CertFreeCertificateContext(items[i]);
    CertFreeCertificateContext(legacy); CertFreeCertificateContext(wrong);
    CertCloseStore(store,0); NCryptDeleteKey(key,0); NCryptFreeObject(provider);
    printf("Certificate selection: failures=%d (memory store only)\n",failed); return failed ? 1:0;
}
