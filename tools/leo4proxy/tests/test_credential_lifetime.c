/* Ownership test: fake SSPI destruction, real SRW synchronization; no network. */
#include "credential_lifetime.h"
#include <stdio.h>
static LONG disposed;
static SECURITY_STATUS SEC_ENTRY fake_free(PCredHandle handle) {
    (void)handle; InterlockedIncrement(&disposed); return SEC_E_OK;
}
#define FreeCredentialsHandle fake_free
#include "../src/credential_lifetime.c"
#undef FreeCredentialsHandle
static DWORD WINAPI borrower(void* argument) {
    CredHandle* handle = (CredHandle*)argument;
    for (int i=0; i<5000; i++) {
        void* lease = credential_borrow(handle);
        if (lease) { Sleep(0); credential_release(lease); }
    }
    return 0;
}
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"line %d failed\n",__LINE__); return 1; } } while(0)
int main(void) {
    CredHandle owner = {11,22}, copy = owner;
    CHECK(credential_register(&owner, NULL));
    copy=owner;
    void* first = credential_borrow(&owner);
    void* second = credential_borrow(&owner);
    CHECK(first && second);
    credential_retire(&owner);
    CHECK(!SecIsValidHandle(&owner) && disposed == 0);
    CHECK(!credential_borrow(&copy));
    credential_release(first); CHECK(disposed == 0);
    credential_release(second); CHECK(disposed == 1);
    credential_retire(&copy); CHECK(disposed == 1);
    owner.dwLower=33; owner.dwUpper=44; copy=owner;
    CHECK(credential_register(&owner, NULL));
    copy=owner;
    HANDLE threads[4];
    for (int i=0;i<4;i++) { threads[i]=CreateThread(NULL,0,borrower,&copy,0,NULL); CHECK(threads[i]); }
    credential_retire(&owner);
    CHECK(WaitForMultipleObjects(4,threads,TRUE,10000) == WAIT_OBJECT_0);
    for(int i=0;i<4;i++) CloseHandle(threads[i]);
    CHECK(disposed==2 && credentials==NULL);
    CredHandle recycled={11,22}, stale=copy;
    CHECK(credential_register(&recycled,NULL));
    CHECK(!credential_borrow(&stale));
    void* current=credential_borrow(&recycled); CHECK(current);
    CHECK(credential_handle(current)->dwLower==11);
    credential_retire(&recycled); credential_release(current); CHECK(disposed==3);
    puts("Credential lifetime: PASS (retirement, outstanding sessions, concurrency)");
    return 0;
}
