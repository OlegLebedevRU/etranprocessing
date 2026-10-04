/* No CA, terminal store, broker or production changes. Task-owned HKCU recovery fixture only. */
#include "../src/renewal.c"
#include <assert.h>
int main(void) {
    BYTE a[32],b[32];assert(hash_pin("000000",a) && hash_pin("000001",b) && memcmp(a,b,32));
    wchar_t path[128];swprintf_s(path,128,L"Software\\Leo4TestRenewal-%lu",GetCurrentProcessId());
    HKEY key=NULL;assert(RegCreateKeyExW(HKEY_CURRENT_USER,path,0,NULL,0,KEY_ALL_ACCESS,NULL,&key,NULL)==ERROR_SUCCESS);
    PendingRenewal* first=(PendingRenewal*)calloc(1,sizeof(*first));
    PendingRenewal* second=(PendingRenewal*)calloc(1,sizeof(*second));assert(first && second);
    assert(pending_load(key,first)==0);first->version=PENDING_VERSION;
    memcpy(first->pin_hash,a,32);wcscpy_s(first->key,128,L"test-nonexistent-key");
    strcpy_s(first->csr,sizeof(first->csr),"test-identical-CSR");
    assert(pending_save(key,first) && pending_load(key,second)==1 && !memcmp(first,second,sizeof(*first)));
    strcpy_s(first->pkcs7,sizeof(first->pkcs7),"test-public-response");
    assert(pending_save(key,first) && pending_load(key,second)==1 && !memcmp(first,second,sizeof(*first)));
    assert(RegSetValueExW(key,L"Pending",0,REG_BINARY,a,32)==ERROR_SUCCESS && pending_load(key,second)==-1);
    RegCloseKey(key);assert(RegDeleteKeyW(HKEY_CURRENT_USER,path)==ERROR_SUCCESS);
    free(first);free(second);
    char* response=NULL;
    assert(!post("http://example.invalid/api/certificates",NULL,"{}",GetTickCount64()+500,&response));
    assert(!post("http://127.0.0.1/api/certificates?pin=invalid",NULL,"{}",GetTickCount64()+500,&response));
    assert(!post("https://example.invalid/api/certificates",NULL,"{}",GetTickCount64(),&response));
    puts("Renewal recovery: DPAPI same-CSR/public-response/corruption and endpoint/deadline guards passed");return 0;
}
