#pragma once
#include <windows.h>
#include <stdbool.h>

/* Shared by setup and the future SYSTEM updater. Never derived from environment
 * variables or a writable 'current' junction. MAX_PATH is an explicit Win7 bound. */
typedef struct {
    wchar_t binaries[MAX_PATH];
    wchar_t data[MAX_PATH];
    wchar_t release[MAX_PATH];
    wchar_t launchers[MAX_PATH];
    wchar_t config[MAX_PATH];
    wchar_t state[MAX_PATH];
    wchar_t logs[MAX_PATH];
    wchar_t operations[MAX_PATH];
    wchar_t cache[MAX_PATH];
    wchar_t staging[MAX_PATH];
} L4Layout;

bool l4_layout_resolve(L4Layout* layout, const wchar_t* version);
/* Explicit roots for isolated tests; both must be absolute local directories,
 * disjoint and canonical. This function performs no filesystem changes. */
bool l4_layout_from_roots(L4Layout* layout, const wchar_t* binaries,
                          const wchar_t* data, const wchar_t* version);
bool l4_layout_component(const L4Layout* layout, const wchar_t* component,
                         const wchar_t* relative_file, wchar_t out[MAX_PATH]);
/* Creates protected directories only. Rejects reparse points at every ancestor.
 * SYSTEM and Administrators: full control; Users: read/execute, no write.
 * Account-specific writable leaves are granted separately before deployment. */
bool l4_layout_prepare(const L4Layout* layout);
/* Versioned signed setup hosts only; independent frozen rollback helper untouched. */
bool l4_layout_prepare_installer(const L4Layout* layout,wchar_t executable[MAX_PATH]);
/* Data leaves only; protected owner/DACL, scoped Modify (never WRITE_DAC).
 * A private leaf has no Users read ACE. Does not alter parent/root permissions. */
bool l4_layout_data_path(const L4Layout* layout, const wchar_t* relative, wchar_t out[MAX_PATH]);
bool l4_layout_prepare_leaf(const L4Layout* layout, const wchar_t* relative,
                            PSID reader, PSID writer, bool private_leaf);

typedef enum { L4_DATA_CONFIG, L4_DATA_STATE, L4_DATA_LOGS } L4DataArea;
/* Installed canonical release -> ProgramData. Explicit portable development
 * tree -> its own files, for isolated tests and unpacked build tools. No migration. */
bool l4_runtime_path(const wchar_t* release, L4DataArea area,
                     const wchar_t* relative, const wchar_t* portable_relative,
                     wchar_t out[MAX_PATH]);
/* Resolve from an actual component EXE (component/[bin/][x86|x64/]file).
 * Installed data uses ProgramData; portable data stays beside the EXE.
 * No environment overrides, filesystem writes or fallback after invalid layout. */
bool l4_runtime_exe_path(const wchar_t* exe, const wchar_t* component,
                         L4DataArea area, const wchar_t* relative,
                         const wchar_t* portable_relative, wchar_t out[MAX_PATH]);
bool l4_runtime_release_from_exe(const wchar_t* exe, const wchar_t* component,
                                 wchar_t release[MAX_PATH], bool* installed);
/* Adapter for existing ANSI Win32 consumers. Rejects lossy conversion. */
bool l4_runtime_path_ansi(const char* release, L4DataArea area,
                          const wchar_t* relative, const wchar_t* portable_relative,
                          char out[MAX_PATH]);

typedef struct {
    bool installed;
    wchar_t account[256];
    wchar_t image_path[2048];
    DWORD start_type;
} L4ServiceInventory;
/* Read-only SCM inspection. Missing service is successful installed=false;
 * access denied/query errors are failures, never treated as a missing service. */
bool l4_service_inventory(const wchar_t* name, L4ServiceInventory* inventory);

/* Shared writable leaf: two explicit actor SIDs, never broad user groups. */
bool l4_layout_prepare_shared_leaf(const L4Layout* layout, const wchar_t* relative,
                                   PSID reader, PSID writer, PSID second_writer, bool private_leaf);

/* Scoped owner restoration for transactional file ACLs. End consumes saved token. */
bool l4_layout_owner_begin(HANDLE* previous);
bool l4_layout_owner_end(HANDLE previous);
