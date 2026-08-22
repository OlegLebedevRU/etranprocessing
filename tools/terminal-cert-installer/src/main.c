#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <stdbool.h>

#include "http_client.h"
#include "xml_utils.h"
#include "cng_crypto.h"
#include "cert_store.h"

#define DEFAULT_URL "https://iot-processing.ru/certificates/Dispatcher.ashx"
#define DEFAULT_KEY_NAME L"EtranTerminalKey"

static void print_usage(const char* prog_name) {
    printf("Etran Terminal Certificate Installer (v=26 CNG Flow)\n\n");
    printf("Usage:\n");
    printf("  %s <PIN> [options]\n", prog_name);
    printf("  %s --pin <PIN> [options]\n", prog_name);
    printf("  %s --status [--store <machine|user>] [--email <email>]\n\n", prog_name);
    printf("Options:\n");
    printf("  --pin, -p <PIN>        6-character terminal certificate PIN\n");
    printf("  --url, -u <URL>        Base URL (default: %s)\n", DEFAULT_URL);
    printf("  --store, -s <STORE>    Target store: 'machine' (LocalMachine\\MY, default) or 'user' (CurrentUser\\MY)\n");
    printf("  --key-name, -k <NAME>  CNG key container name (default: EtranTerminalKey)\n");
    printf("  --status, -l           List certificates in target store\n");
    printf("  --help, -h             Show this help message\n\n");
    printf("Examples:\n");
    printf("  %s B75GL9\n", prog_name);
    printf("  %s --pin B75GL9 --store machine\n", prog_name);
}

int main(int argc, char* argv[]) {
    // Set console output to UTF-8 for clean Russian/English text output
    SetConsoleOutputCP(CP_UTF8);

    char pin[64] = { 0 };
    char base_url[512] = DEFAULT_URL;
    bool is_machine_store = true; // Default is LocalMachine\MY as requested
    WCHAR key_name[128] = DEFAULT_KEY_NAME;
    bool status_mode = false;
    char filter_email[256] = { 0 };

    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--help") == 0 || _stricmp(argv[i], "-h") == 0 || _stricmp(argv[i], "/?") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (_stricmp(argv[i], "--status") == 0 || _stricmp(argv[i], "-l") == 0 || _stricmp(argv[i], "--list") == 0) {
            status_mode = true;
        } else if (_stricmp(argv[i], "--pin") == 0 || _stricmp(argv[i], "-p") == 0) {
            if (i + 1 < argc) {
                strncpy(pin, argv[++i], sizeof(pin) - 1);
            }
        } else if (_stricmp(argv[i], "--url") == 0 || _stricmp(argv[i], "-u") == 0) {
            if (i + 1 < argc) {
                strncpy(base_url, argv[++i], sizeof(base_url) - 1);
            }
        } else if (_stricmp(argv[i], "--store") == 0 || _stricmp(argv[i], "-s") == 0) {
            if (i + 1 < argc) {
                const char* s = argv[++i];
                if (_stricmp(s, "user") == 0 || _stricmp(s, "currentuser") == 0) {
                    is_machine_store = false;
                } else {
                    is_machine_store = true;
                }
            }
        } else if (_stricmp(argv[i], "--key-name") == 0 || _stricmp(argv[i], "-k") == 0) {
            if (i + 1 < argc) {
                MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1, key_name, 128);
            }
        } else if (_stricmp(argv[i], "--email") == 0 || _stricmp(argv[i], "-e") == 0) {
            if (i + 1 < argc) {
                strncpy(filter_email, argv[++i], sizeof(filter_email) - 1);
            }
        } else if (argv[i][0] != '-' && pin[0] == '\0') {
            // Positional argument = PIN
            strncpy(pin, argv[i], sizeof(pin) - 1);
        }
    }

    if (status_mode) {
        cert_store_list(is_machine_store, filter_email);
        return 0;
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

    printf("=================================================================\n");
    printf("  Etran Terminal Certificate Setup (CNG / v=26)\n");
    printf("=================================================================\n");
    printf("Target Server:       %s\n", base_url);
    printf("PIN Code:            %s\n", pin);
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
            fprintf(stderr, "Response: %s\n", check_xml);
            free(check_xml);
        }
        return 2;
    }

    CheckResponse check_resp;
    if (!parse_check_response(check_xml, &check_resp)) {
        fprintf(stderr, "[ERROR] CHECK failed: code=%d, description='%s'\n",
                check_resp.code, check_resp.description[0] ? check_resp.description : check_xml);
        free(check_xml);
        return 2;
    }
    free(check_xml);

    printf("      Provider: %s\n", check_resp.prov);
    printf("      DN:       %s\n", check_resp.dn);
    printf("      Sign:     %s\n", check_resp.sign);

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
    printf("      CSR DN:   %s\n", dn_with_sign);

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
            fprintf(stderr, "Response: %s\n", setup_xml);
            free(setup_xml);
        }
        free(pkcs10_b64);
        return 4;
    }
    free(pkcs10_b64);

    SetupResponse setup_resp;
    if (!parse_setup_response(setup_xml, &setup_resp)) {
        fprintf(stderr, "[ERROR] SETUP failed: code=%d, description='%s'\n",
                setup_resp.code, setup_resp.description[0] ? setup_resp.description : (setup_xml ? setup_xml : ""));
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
    printf("      Deleting older certificates with matching email [%s]...\n", target_email);

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
