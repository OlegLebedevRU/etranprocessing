#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>

#include "http_client.h"
#include "url_finder.h"
#include "xml_utils.h"
#include "cng_crypto.h"
#include "cert_store.h"
#include "cert_discovery.h"
#include "gui.h"

#define DEFAULT_KEY_NAME L"EtranTerminalKey"

static bool create_fresh_key_name(WCHAR* out, size_t capacity) {
    HCRYPTPROV provider = 0;
    BYTE random_bytes[16] = { 0 };
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return false;
    BOOL ok = CryptGenRandom(provider, sizeof(random_bytes), random_bytes);
    CryptReleaseContext(provider, 0);
    if (!ok) return false;
    int written = swprintf_s(out, capacity,
        L"EtranTerminalKey-%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
        random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
        random_bytes[4], random_bytes[5], random_bytes[6], random_bytes[7],
        random_bytes[8], random_bytes[9], random_bytes[10], random_bytes[11],
        random_bytes[12], random_bytes[13], random_bytes[14], random_bytes[15]);
    SecureZeroMemory(random_bytes, sizeof(random_bytes));
    return written > 0;
}

static void print_usage(const char* prog_name) {
    printf("Leo4 Terminal Certificate Installer - l4pin (v=26 CNG Flow)\n\n");
    printf("Usage:\n");
    printf("  %s <PIN> [options]\n", prog_name);
    printf("  %s --pin <PIN> [options]\n", prog_name);
    printf("  %s -pin <PIN> [options]\n", prog_name);
    printf("  %s --check [--sn <SN>] [--json] [--store <machine|user>]\n", prog_name);
    printf("  %s --status [--store <machine|user>] [--email <email>]\n\n", prog_name);
    printf("Options:\n");
    printf("  --check                  Check certificate status in target store and exit\n");
    printf("  --sn <SN>                Expected terminal serial number / Common Name\n");
    printf("  --json                   Output in JSON format (used with --check)\n");
    printf("  --force, -f              Force reissue even if valid certificate exists\n");
    printf("  --pin, -pin, -p <PIN>    6-character terminal certificate PIN code\n");
    printf("  --url, -url, -u <URL>    Base URL override (default: auto-detected via leo4proxy -> fallback)\n");
    printf("  --store, -store, -s <S>  Target store: 'machine' (LocalMachine\\MY, default) or 'user' (CurrentUser\\MY)\n");
    printf("  --key-name, -k <NAME>    Explicit CNG key container name (must be unused)\n");
    printf("  --email, -e <EMAIL>      Filter certificates by email (for --status)\n");
    printf("  --status, -l, --list     List installed certificates in target store\n");
    printf("  --help, -h, /?           Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s 021358\n", prog_name);
    printf("  %s --check --json\n", prog_name);
    printf("  %s --check --sn a4b0000773c82116d210826\n", prog_name);
    printf("  %s --force 021358\n", prog_name);
    printf("  %s --pin 021358 --store machine\n", prog_name);
    printf("  %s --status\n", prog_name);
}

static bool process_is_elevated(void) {
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation = { 0 };
    DWORD size = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}

static int run_elevated_copy(void) {
    wchar_t exe[MAX_PATH] = { 0 };
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return 20;
    const wchar_t* params = GetCommandLineW();
    if (*params == L'"') {
        params++;
        while (*params && *params != L'"') params++;
        if (*params) params++;
    } else {
        while (*params && *params != L' ' && *params != L'\t') params++;
    }
    while (*params == L' ' || *params == L'\t') params++;
    SHELLEXECUTEINFOW info = { 0 };
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exe;
    info.lpParameters = params;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        fprintf(stderr, "Machine certificate store requires administrator privileges (UAC error %lu).\n", GetLastError());
        return 20;
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD result = 20;
    GetExitCodeProcess(info.hProcess, &result);
    CloseHandle(info.hProcess);
    return (int)result;
}

int l4pin_run_cli(int argc, char* argv[]) {
    // Set console output to UTF-8 for clean Russian/English text output
    SetConsoleOutputCP(CP_UTF8);

    char pin[64] = { 0 };
    char cli_url[512] = { 0 };
    bool cli_url_provided = false;
    char base_url[512] = { 0 };
    bool is_machine_store = true; // Default is LocalMachine\MY
    WCHAR key_name[128] = { 0 };
    bool key_name_explicit = false;
    bool status_mode = false;
    bool check_mode = false;
    bool is_json = false;
    bool force_reissue = false;
    char cli_sn[128] = { 0 };
    char filter_email[256] = { 0 };

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (_stricmp(arg, "--help") == 0 || _stricmp(arg, "-help") == 0 ||
            _stricmp(arg, "-h") == 0 || _stricmp(arg, "/help") == 0 ||
            _stricmp(arg, "/h") == 0 || _stricmp(arg, "/?") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (_stricmp(arg, "--check") == 0 || _stricmp(arg, "-check") == 0 ||
                   _stricmp(arg, "/check") == 0) {
            check_mode = true;
        } else if (_stricmp(arg, "--json") == 0 || _stricmp(arg, "-json") == 0 ||
                   _stricmp(arg, "/json") == 0) {
            is_json = true;
        } else if (_stricmp(arg, "--force") == 0 || _stricmp(arg, "-force") == 0 ||
                   _stricmp(arg, "-f") == 0 || _stricmp(arg, "/force") == 0 ||
                   _stricmp(arg, "/f") == 0) {
            force_reissue = true;
        } else if (_stricmp(arg, "--sn") == 0 || _stricmp(arg, "-sn") == 0 ||
                   _stricmp(arg, "/sn") == 0) {
            if (i + 1 < argc) {
                strncpy(cli_sn, argv[++i], sizeof(cli_sn) - 1);
            }
        } else if (_strnicmp(arg, "--sn=", 5) == 0) {
            strncpy(cli_sn, arg + 5, sizeof(cli_sn) - 1);
        } else if (_strnicmp(arg, "-sn=", 4) == 0) {
            strncpy(cli_sn, arg + 4, sizeof(cli_sn) - 1);
        } else if (_stricmp(arg, "--status") == 0 || _stricmp(arg, "-status") == 0 ||
                   _stricmp(arg, "-l") == 0 || _stricmp(arg, "--list") == 0 ||
                   _stricmp(arg, "-list") == 0 || _stricmp(arg, "/status") == 0 ||
                   _stricmp(arg, "/list") == 0) {
            status_mode = true;
        } else if (_stricmp(arg, "--pin") == 0 || _stricmp(arg, "-pin") == 0 ||
                   _stricmp(arg, "-p") == 0 || _stricmp(arg, "/pin") == 0 ||
                   _stricmp(arg, "/p") == 0) {
            if (i + 1 < argc) {
                strncpy(pin, argv[++i], sizeof(pin) - 1);
            }
        } else if (_strnicmp(arg, "--pin=", 6) == 0) {
            strncpy(pin, arg + 6, sizeof(pin) - 1);
        } else if (_strnicmp(arg, "-pin=", 5) == 0) {
            strncpy(pin, arg + 5, sizeof(pin) - 1);
        } else if (_stricmp(arg, "--url") == 0 || _stricmp(arg, "-url") == 0 ||
                   _stricmp(arg, "-u") == 0 || _stricmp(arg, "/url") == 0 ||
                   _stricmp(arg, "/u") == 0) {
            if (i + 1 < argc) {
                strncpy(cli_url, argv[++i], sizeof(cli_url) - 1);
                cli_url_provided = true;
            }
        } else if (_strnicmp(arg, "--url=", 6) == 0) {
            strncpy(cli_url, arg + 6, sizeof(cli_url) - 1);
            cli_url_provided = true;
        } else if (_strnicmp(arg, "-url=", 5) == 0) {
            strncpy(cli_url, arg + 5, sizeof(cli_url) - 1);
            cli_url_provided = true;
        } else if (_stricmp(arg, "--store") == 0 || _stricmp(arg, "-store") == 0 ||
                   _stricmp(arg, "-s") == 0 || _stricmp(arg, "/store") == 0 ||
                   _stricmp(arg, "/s") == 0) {
            if (i + 1 < argc) {
                const char* s = argv[++i];
                if (_stricmp(s, "user") == 0 || _stricmp(s, "currentuser") == 0) {
                    is_machine_store = false;
                } else {
                    is_machine_store = true;
                }
            }
        } else if (_stricmp(arg, "--key-name") == 0 || _stricmp(arg, "-key-name") == 0 ||
                   _stricmp(arg, "-k") == 0 || _stricmp(arg, "/key-name") == 0 ||
                   _stricmp(arg, "/k") == 0) {
            if (i + 1 < argc) {
                MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1, key_name, 128);
                key_name_explicit = true;
            }
        } else if (_stricmp(arg, "--email") == 0 || _stricmp(arg, "-email") == 0 ||
                   _stricmp(arg, "-e") == 0 || _stricmp(arg, "/email") == 0 ||
                   _stricmp(arg, "/e") == 0) {
            if (i + 1 < argc) {
                strncpy(filter_email, argv[++i], sizeof(filter_email) - 1);
            }
        } else if (arg[0] != '-' && arg[0] != '/' && pin[0] == '\0') {
            // Positional argument = PIN
            strncpy(pin, arg, sizeof(pin) - 1);
        }
    }

    if (!status_mode && !check_mode && is_machine_store && !process_is_elevated()) {
        return run_elevated_copy();
    }

    if (status_mode) {
        cert_store_list(is_machine_store, filter_email);
        return 0;
    }

    if (check_mode) {
        wchar_t w_sn[128] = { 0 };
        const wchar_t* p_expected_sn = NULL;
        if (cli_sn[0] != '\0') {
            MultiByteToWideChar(CP_UTF8, 0, cli_sn, -1, w_sn, 128);
            p_expected_sn = w_sn;
        }

        cert_info info = { 0 };
        cert_state st;
        if (is_machine_store) {
            st = cert_discover(p_expected_sn, &info);
        } else {
            HCERTSTORE hUserStore = CertOpenStore(
                CERT_STORE_PROV_SYSTEM_W,
                0,
                0,
                CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG,
                L"MY"
            );
            if (hUserStore) {
                st = cert_discover_in_store(hUserStore, p_expected_sn, &info);
                CertCloseStore(hUserStore, 0);
            } else {
                st = CERT_STORE_ERROR;
            }
        }

        if (st >= 20) {
            if (is_json) {
                printf("{\n  \"state\": \"error\",\n  \"error_code\": %d,\n  \"message\": \"Failed to access certificate store (administrator privileges may be required)\"\n}\n", (int)st);
            } else {
                fprintf(stderr, "Error: Failed to access certificate store (error %d). Administrator privileges may be required.\n", (int)st);
            }
            return (int)st;
        }

        if (is_json) {
            printf("{\n  \"state\": \"%s\",\n  \"thumbprint\": \"%s\",\n  \"sn\": \"%s\",\n  \"not_after\": \"%s\",\n  \"days_left\": %d\n}\n",
                   cert_state_to_str(st),
                   info.thumbprint_hex,
                   info.sn,
                   info.not_after_utc,
                   info.days_left);
        } else {
            printf("Certificate Discovery Status:\n");
            printf("  State:       %s\n", cert_state_to_str(st));
            printf("  Thumbprint:  %s\n", info.thumbprint_hex[0] ? info.thumbprint_hex : "(none)");
            printf("  SN:          %s\n", info.sn[0] ? info.sn : "(none)");
            printf("  Not After:   %s\n", info.not_after_utc[0] ? info.not_after_utc : "(none)");
            printf("  Days Left:   %d\n", info.days_left);
            printf("  Duplicates:  %d\n", info.cert_duplicates);
        }

        return (int)st;
    }

    if (pin[0] == '\0') {
        printf("Enter terminal PIN code: ");
        if (fgets(pin, sizeof(pin), stdin)) {
            char* p = strchr(pin, '\n');
            if (p) *p = '\0';
            p = strchr(pin, '\r');
            if (p) *p = '\0';
        }
    }

    if (pin[0] == '\0') {
        fprintf(stderr, "Error: PIN code is required.\n");
        print_usage(argv[0]);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Certificate Discovery & Guard (Contract 4.1)
    // -----------------------------------------------------------------------
    wchar_t w_disc_sn[128] = { 0 };
    const wchar_t* p_disc_expected_sn = NULL;
    if (cli_sn[0] != '\0') {
        MultiByteToWideChar(CP_UTF8, 0, cli_sn, -1, w_disc_sn, 128);
        p_disc_expected_sn = w_disc_sn;
    }

    cert_info disc_info = { 0 };
    cert_state disc_st;
    if (is_machine_store) {
        disc_st = cert_discover(p_disc_expected_sn, &disc_info);
    } else {
        HCERTSTORE hUserStore = CertOpenStore(
            CERT_STORE_PROV_SYSTEM_W,
            0,
            0,
            CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG,
            L"MY"
        );
        if (hUserStore) {
            disc_st = cert_discover_in_store(hUserStore, p_disc_expected_sn, &disc_info);
            CertCloseStore(hUserStore, 0);
        } else {
            disc_st = CERT_STORE_ERROR;
        }
    }

    printf("[CERT DISCOVERY] state: %s, thumbprint: %s, days_left: %d, duplicates: %d\n",
           cert_state_to_str(disc_st),
           disc_info.thumbprint_hex[0] ? disc_info.thumbprint_hex : "(none)",
           disc_info.days_left,
           disc_info.cert_duplicates);

    if (disc_st == CERT_VALID && !force_reissue) {
        printf("Certificate %s for %s is valid until %s; reissue not required (use --force to override)\n",
               disc_info.thumbprint_hex,
               disc_info.sn[0] ? disc_info.sn : "(unknown)",
               disc_info.not_after_utc);
        return 0;
    }

    if (disc_st >= CERT_STORE_ERROR) {
        fprintf(stderr, "[ERROR] Certificate discovery failed; refusing to replace an unverified certificate.\n");
        return (int)disc_st;
    }

    if (!key_name_explicit && !create_fresh_key_name(key_name, sizeof(key_name) / sizeof(key_name[0]))) {
        fprintf(stderr, "[ERROR] Cannot create a fresh CNG container name.\n");
        return 3;
    }

    if (disc_st == CERT_EXPIRING && !force_reissue) {
        printf("[WARN] Existing certificate %s for %s is expiring in %d days (until %s). Proceeding with renewal...\n",
               disc_info.thumbprint_hex,
               disc_info.sn[0] ? disc_info.sn : "(unknown)",
               disc_info.days_left,
               disc_info.not_after_utc);
    } else if (disc_st == CERT_BROKEN) {
        printf("[WARN] Existing certificate is broken or mismatch (state: broken). Proceeding with enrollment...\n");
    } else if (disc_st == CERT_ABSENT) {
        printf("[INFO] No existing certificate found (state: absent). Proceeding with enrollment...\n");
    } else if (force_reissue) {
        printf("[WARN] Force reissue requested (--force). Reissuing certificate regardless of current state...\n");
    }

    // Resolve base URL via url-finder strategy
    if (!resolve_certificates_url(cli_url_provided ? cli_url : NULL, base_url, sizeof(base_url))) {
        fprintf(stderr, "[ERROR] Failed to resolve target certificates URL.\n");
        return 1;
    }

    printf("\n=================================================================\n");
    printf("  Leo4 Terminal Certificate Setup - l4pin (CNG / v=26)\n");
    printf("=================================================================\n");
    printf("Target Endpoint:     %s\n", base_url);
    printf("PIN Code:            ***\n");
    printf("Target Store:        %s\\MY\n", is_machine_store ? "LocalMachine" : "CurrentUser");
    printf("CNG Key Container:   %ls\n", key_name);
    printf("-----------------------------------------------------------------\n\n");

    // -----------------------------------------------------------------------
    // Step 1: CHECK (v=26)
    // -----------------------------------------------------------------------
    char tosign[256] = { 0 };
    if (!generate_tosign(tosign, sizeof(tosign))) {
        fprintf(stderr, "[ERROR] Failed to generate tosign data.\n");
        return 1;
    }

    printf("[1/5] Requesting certificate parameters (CHECK v=26)...\n");
    char* check_xml = NULL;
    size_t check_xml_len = 0;
    if (!http_check(base_url, pin, tosign, "26", &check_xml, &check_xml_len)) {
        fprintf(stderr, "[ERROR] HTTP CHECK request failed.\n");
        if (check_xml) {
            free(check_xml);
        }
        return 2;
    }

    CheckResponse check_resp;
    if (!parse_check_response(check_xml, &check_resp)) {
        fprintf(stderr, "[ERROR] CHECK failed: code=%d, description='%s'\n",
                check_resp.code, check_resp.description[0] ? check_resp.description : "No server description");
        free(check_xml);
        return 2;
    }
    free(check_xml);

    printf("      Provider: %s\n", check_resp.prov);

    if (check_resp.sign[0] == '\0') {
        fprintf(stderr, "[ERROR] Server did not return 'sign' in CHECK response.\n");
        return 2;
    }

    // -----------------------------------------------------------------------
    // Step 2: Extract Email and Replace CN with Sign in Subject DN
    // -----------------------------------------------------------------------
    char target_email[256] = { 0 };
    extract_email_from_dn(check_resp.dn, target_email, sizeof(target_email));
    if (target_email[0] != '\0') {
        printf("      Email:    %s\n", target_email);
    }

    char* dn_with_sign = replace_cn_in_dn(check_resp.dn, check_resp.sign);
    if (!dn_with_sign) {
        fprintf(stderr, "[ERROR] Failed to format modified Subject DN.\n");
        return 3;
    }

    // -----------------------------------------------------------------------
    // Step 3: Generate Non-Exportable RSA 2048 Key in CNG & Create PKCS#10 CSR
    // -----------------------------------------------------------------------
    printf("\n[2/5] Generating non-exportable RSA 2048 key in CNG (KSP)...\n");
    WCHAR w_prov[256] = { 0 };
    if (check_resp.prov[0] != '\0') {
        MultiByteToWideChar(CP_UTF8, 0, check_resp.prov, -1, w_prov, 256);
    }

    char* pkcs10_b64 = NULL;
    if (!cng_generate_key_and_csr(
            w_prov[0] ? w_prov : NULL,
            key_name,
            dn_with_sign,
            2048,
            is_machine_store,
            &pkcs10_b64
        )) {
        fprintf(stderr, "[ERROR] Failed to generate CNG key and PKCS#10 CSR.\n");
        free(dn_with_sign);
        return 3;
    }
    free(dn_with_sign);
    printf("      Generated PKCS#10 CSR (%zu bytes Base64)\n", strlen(pkcs10_b64));

    // -----------------------------------------------------------------------
    // Step 4: SETUP (Submit CSR, Receive PKCS#7 Chain)
    // -----------------------------------------------------------------------
    printf("\n[3/5] Submitting CSR for signing (SETUP)...\n");
    char* setup_xml = NULL;
    size_t setup_xml_len = 0;
    if (!http_setup(base_url, pin, check_resp.sign, pkcs10_b64, &setup_xml, &setup_xml_len)) {
        fprintf(stderr, "[ERROR] HTTP SETUP request failed.\n");
        if (setup_xml) {
            free(setup_xml);
        }
        free(pkcs10_b64);
        return 4;
    }
    free(pkcs10_b64);

    SetupResponse setup_resp;
    if (!parse_setup_response(setup_xml, &setup_resp)) {
        fprintf(stderr, "[ERROR] SETUP failed: code=%d, description='%s'\n",
                setup_resp.code, setup_resp.description[0] ? setup_resp.description : "No server description");
        if (setup_xml) free(setup_xml);
        free_setup_response(&setup_resp);
        return 4;
    }
    if (setup_xml) free(setup_xml);

    printf("      Received signed PKCS#7 chain (%zu bytes Base64)\n", setup_resp.certdata_len);

    // -----------------------------------------------------------------------
    // Step 5: Install Certificate in Windows Store & Clean by Email Filter
    // -----------------------------------------------------------------------
    printf("\n[4/5] Installing certificate to %s\\MY...\n", is_machine_store ? "LocalMachine" : "CurrentUser");
    printf("      Existing certificates will be removed only after the new certificate and key are verified.\n");

    CertDetails installed_details;
    if (!cert_store_install_pkcs7(
            setup_resp.certdata,
            key_name,
            is_machine_store,
            target_email,
            &installed_details
        )) {
        fprintf(stderr, "[ERROR] Failed to install certificate into Windows Certificate Store.\n");
        free_setup_response(&setup_resp);
        return 5;
    }
    free_setup_response(&setup_resp);

    // -----------------------------------------------------------------------
    // Success Report
    // -----------------------------------------------------------------------
    printf("\n[5/5] Certificate installation and key binding completed successfully!\n");
    printf("=================================================================\n");
    printf("  INSTALLED CERTIFICATE DETAILS\n");
    printf("=================================================================\n");
    printf("Subject:         %s\n", installed_details.subject);
    printf("Issuer:          %s\n", installed_details.issuer);
    printf("Serial Number:   %s\n", installed_details.serial);
    printf("Email:           %s\n", installed_details.email);
    printf("Thumbprint:      %s\n", installed_details.thumbprint);
    printf("Valid From:      %s\n", installed_details.not_before);
    printf("Valid To:        %s\n", installed_details.not_after);
    printf("Private Key:     %s\n", installed_details.has_private_key ? "BOUND & ACCESSIBLE via CNG (Non-Exportable)" : "ERROR: NOT ACCESSIBLE");
    printf("Store Location:  %s\\MY\n", is_machine_store ? "LocalMachine" : "CurrentUser");
    printf("Key Container:   %ls\n", key_name);
    printf("=================================================================\n");

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        if (!process_is_elevated()) return run_elevated_copy();
        FreeConsole();
        return l4pin_show_gui();
    }
    return l4pin_run_cli(argc, argv);
}
