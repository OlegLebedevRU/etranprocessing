#pragma once
#include "install_bundle.h"
#include "../../l4common/bootstrap.h"
#include "../../l4common/access.h"
typedef struct SetupInstalledSource SetupInstalledSource;
/* Primary SYSTEM, existing owner journal/deployment lock. Discovers an original
 * committed fresh operation or authenticated remote102 successor/predecessor
 * chain with both recovery decisions COMMITTED. No caller version/UUID/root.
 * Full inventory and SCM source authentication only, never readiness/apply.
 * Retains the approved physical-console token/session/logon identity as the
 * fifth ACL actor; verify re-queries it and refuses logoff/identity drift.
 * Borrows original journal: source/getters/verify are valid ONLY while that
 * journal remains owned and open. Copy needed typed data before worker journal
 * handoff; never source_verify after transfer closes the journal. */
bool setup_installed_source_open(L4Journal* owner,SetupInstalledSource** source);
bool setup_installed_source_verify(SetupInstalledSource* source);
void setup_installed_source_close(SetupInstalledSource* source);
const L4BootstrapPlan* setup_installed_source_plan(const SetupInstalledSource* source);
const SetupManifest* setup_installed_source_manifest(const SetupInstalledSource* source);
const SetupRootManifest* setup_installed_source_root(const SetupInstalledSource* source);
/* Original authenticated active updater anchor, independent of suite root. */
const SetupRootManifest* setup_installed_source_updater_root(const SetupInstalledSource* source);
const wchar_t* setup_installed_source_operation(const SetupInstalledSource* source);
const char* setup_installed_source_version(const SetupInstalledSource* source);
const char* setup_installed_source_suite_version(const SetupInstalledSource* source);
const char* setup_installed_source_updater_version(const SetupInstalledSource* source);
const char* setup_installed_source_arch(const SetupInstalledSource* source);
const L4AccessActors* setup_installed_source_actors(const SetupInstalledSource* source);

/* Thread-local, sanitized last discovery stage; no source authority or inputs. */
const char* setup_installed_source_stage(void);
const wchar_t* setup_installed_source_candidate(void);

/* Fixed authenticated current config20 candidate bytes; allocated output, caller frees.
 * index0 supervisor,1 broker,2 ACL;3..11 fresh fixed nine-tool launcher order.
 * Original fresh candidates require absent old file; remote102 uses exact64 refs.
 * No original SD authority: current bytes/policy must be checked independently. */
bool setup_installed_source_config(SetupInstalledSource* source,unsigned index,BYTE** bytes,DWORD* size);
bool setup_installed_source_owned_by(const SetupInstalledSource* source,const L4Journal* journal);
