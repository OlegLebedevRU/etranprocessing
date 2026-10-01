/* Exercise the production cleanup using isolated HKCU registry fixtures.
 * System roots/provider are redirected: no real profile or MY is modified. */
#include <windows.h>
#include <wincrypt.h>
#include <sddl.h>
#include <stdio.h>
#include "cert_store.h"
#include "cert_discovery.h"

static HKEY fixture_machine, fixture_users;
static HCERTSTORE fixture_machine_my;
static bool fail_delete;
static HCERTSTORE WINAPI fixture_open(LPCSTR provider, DWORD encoding,
    HCRYPTPROV_LEGACY crypt, DWORD flags, const void* parameter) {
    if (provider == CERT_STORE_PROV_SYSTEM_W &&
        (flags & CERT_SYSTEM_STORE_LOCATION_MASK) == CERT_SYSTEM_STORE_LOCAL_MACHINE)
        return CertDuplicateStore(fixture_machine_my);
    return CertOpenStore(provider, encoding, crypt, flags, parameter);
}
static BOOL WINAPI fixture_delete(PCCERT_CONTEXT cert) {
    char email[256];
    if (fail_delete && cert_get_email(cert, email, sizeof(email)) && !strcmp(email, "fail.terminal@leo4.ru")) {
        CertFreeCertificateContext(cert); /* Delete always consumes the argument. */
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    return CertDeleteCertificateFromStore(cert);
}

#undef HKEY_LOCAL_MACHINE
#define HKEY_LOCAL_MACHINE fixture_machine
#undef HKEY_USERS
#define HKEY_USERS fixture_users
#define CertOpenStore fixture_open
#define CertDeleteCertificateFromStore fixture_delete
#include "../src/cert_store.c"
#undef CertOpenStore
#undef CertDeleteCertificateFromStore
#undef HKEY_LOCAL_MACHINE
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)((LONG)0x80000002))
#undef HKEY_USERS
#define HKEY_USERS ((HKEY)(ULONG_PTR)((LONG)0x80000003))

#define main existing_certificate_tests_main
#include "test_cert_discovery.c"
#undef main

static int errors;
#define REQUIRE(x) do { if (!(x)) { printf("FAIL profile line %d: %s\n", __LINE__, #x); errors++; } } while (0)

static HCERTSTORE registry_store(HKEY root, const wchar_t* path, HKEY* key) {
    REQUIRE(RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, key, NULL) == ERROR_SUCCESS);
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_REG, 0, 0, 0, *key);
    REQUIRE(store != NULL);
    return store;
}
static bool present(HCERTSTORE store, PCCERT_CONTEXT cert) {
    CertControlStore(store, 0, CERT_STORE_CTRL_RESYNC, NULL);
    PCCERT_CONTEXT found = CertFindCertificateInStore(store, X509_ASN_ENCODING, 0,
        CERT_FIND_EXISTING, cert, NULL);
    if (found) CertFreeCertificateContext(found);
    return found != NULL;
}

int main(void) {
    wchar_t fixture_path[128];
    swprintf_s(fixture_path, 128, L"Software\\L4ToolsTests\\ProfileCleanup-%lu-%llu",
               GetCurrentProcessId(), GetTickCount64());
    HKEY root = NULL, machine_key = NULL, profile_key = NULL, my_key = NULL;
    DWORD disposition;
    REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER, fixture_path, 0, NULL, 0, KEY_ALL_ACCESS,
        NULL, &root, &disposition) == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY);
    if (!root) return 1;
    REQUIRE(RegCreateKeyExW(root, L"Machine", 0, NULL, 0, KEY_ALL_ACCESS,
        NULL, &fixture_machine, NULL) == ERROR_SUCCESS);
    REQUIRE(RegCreateKeyExW(root, L"Users", 0, NULL, 0, KEY_ALL_ACCESS,
        NULL, &fixture_users, NULL) == ERROR_SUCCESS);
    REQUIRE(RegCreateKeyExW(fixture_machine,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList\\S-1-5-21-1-2-3-1001",
        0, NULL, 0, KEY_ALL_ACCESS, NULL, &profile_key, NULL) == ERROR_SUCCESS);
    HCERTSTORE profile = registry_store(fixture_users,
        L"S-1-5-21-1-2-3-1001\\Software\\Microsoft\\SystemCertificates\\MY", &my_key);
    fixture_machine_my = registry_store(root, L"MachineMy", &machine_key);

    PCCERT_CONTEXT fresh = create_test_cert(0, "CN=iot.leo4.ru, E=new.terminal@leo4.ru", 90, false);
    PCCERT_CONTEXT previous = create_test_cert(0, "CN=iot.leo4.ru, E=old.terminal@leo4.ru", 90, false);
    PCCERT_CONTEXT legacy = create_test_cert(0, "CN=certsrv, E=fail.terminal@leo4.ru", 90, false);
    PCCERT_CONTEXT unrelated = create_test_cert(0, "CN=certsrv, E=employee@example.org", 90, false);
    REQUIRE(fresh && previous && legacy && unrelated);
    if (!fresh || !previous || !legacy || !unrelated) return 1;
    REQUIRE(CertAddCertificateContextToStore(fixture_machine_my, fresh, CERT_STORE_ADD_ALWAYS, NULL));
    REQUIRE(CertAddCertificateContextToStore(fixture_machine_my, previous, CERT_STORE_ADD_ALWAYS, NULL));
    REQUIRE(CertAddCertificateContextToStore(profile, legacy, CERT_STORE_ADD_ALWAYS, NULL));
    REQUIRE(CertAddCertificateContextToStore(profile, unrelated, CERT_STORE_ADD_ALWAYS, NULL));
    int removed = -1;
    fail_delete = true;
    REQUIRE(!cert_store_cleanup_all_profiles(fixture_machine_my, fresh, &removed));
    REQUIRE(removed == 0);
    REQUIRE(present(fixture_machine_my, previous));
    REQUIRE(present(profile, legacy));
    REQUIRE(present(fixture_machine_my, fresh));
    fail_delete = false;
    REQUIRE(cert_store_cleanup_all_profiles(fixture_machine_my, fresh, &removed));
    REQUIRE(removed == 2);
    REQUIRE(!present(fixture_machine_my, previous));
    REQUIRE(!present(profile, legacy));
    REQUIRE(present(profile, unrelated));
    REQUIRE(present(fixture_machine_my, fresh));
    REQUIRE(cert_store_cleanup_all_profiles(fixture_machine_my, fresh, &removed));
    REQUIRE(removed == 0);
    CertFreeCertificateContext(fresh);
    CertFreeCertificateContext(previous);
    CertFreeCertificateContext(legacy);
    CertFreeCertificateContext(unrelated);
    CertCloseStore(profile, 0);
    CertCloseStore(fixture_machine_my, 0);
    RegCloseKey(my_key);
    RegCloseKey(machine_key);
    RegCloseKey(profile_key);
    RegCloseKey(fixture_users);
    RegCloseKey(fixture_machine);
    RegCloseKey(root);
    REQUIRE(RegDeleteTreeW(HKEY_CURRENT_USER, fixture_path) == ERROR_SUCCESS);
    LONG delete_result = RegDeleteKeyW(HKEY_CURRENT_USER, fixture_path);
    REQUIRE(delete_result == ERROR_SUCCESS || delete_result == ERROR_FILE_NOT_FOUND);
    printf("Cross-profile cleanup failures: %d\n", errors);
    return errors ? 1 : 0;
}
