#pragma once
#include "root_manifest.h"
#include "readiness.h"
#include "../../l4common/access.h"
#include "broker_config.h"

typedef struct SetupFreshInstall SetupFreshInstall;
/* Additional bytes come from trusted local component adapters, never RPC/config
 * filenames. Fixed supported paths only; no defaults or legacy copying. */
typedef struct {const wchar_t* relative;const void* bytes;DWORD size;} SetupFreshConfig;
/* Native SYSTEM host, new empty locked journal, selected production trusted root,
 * exact descriptor/signature and protected archive. Signature/full inventory
 * admission precedes offline ACL provisioning; all four services and destination
 * configs must be absent. Caller has already authenticated catalog selection.
 * Actors must be impersonation tokens (SecurityImpersonation or higher), four
 * service actors are LocalSystem. Retains duplicates/manifest/journal identity. */
bool setup_fresh_prepare(L4Journal* journal,const SetupRootManifest* root,
    const void* descriptor,DWORD descriptor_size,const BYTE* signature,DWORD signature_size,
    const wchar_t* archive,const L4BootstrapProfile profiles[4],const L4AccessActors* actors,
    const SetupFreshConfig* extra,unsigned extra_count,SetupFreshInstall** prepared);
/* Local producer: render an active profile for an authenticated discovered SN,
 * or loopback standby without upstream/client ID when identity is absent.
 * Only these secret-free rendered profiles receive the public-read broker ACL. */
bool setup_fresh_prepare_broker(L4Journal* journal,const SetupRootManifest* root,
    const void* descriptor,DWORD descriptor_size,const BYTE* signature,DWORD signature_size,
    const wchar_t* archive,const L4BootstrapProfile profiles[4],const L4AccessActors* actors,
    const char* authenticated_sn,const SetupFreshConfig* extra,unsigned extra_count,
    SetupFreshInstall** prepared);
/* One in-process invocation; failed invocation remains abortable, not resumable
 * by calling apply twice. No public CLI/migration or helper activation.
 * Caller supplies fixed native checks; aggregate commit budget is explicit. */
bool setup_fresh_apply(L4Journal* journal,SetupFreshInstall* prepared,
    const L4BootstrapChecks* checks,DWORD commit_timeout);
/* Signed local deployment only: no certificate/channel prerequisite. Installs
 * files, configuration, ACLs, PATH and owned stopped SCM/start types, then commits
 * deployment separately from communication READY. Postcommit start is diagnostic. */
bool setup_fresh_deploy_local(L4Journal* journal,SetupFreshInstall* prepared,DWORD timeout);
/* Managed service abort must complete before any reverse configuration rollback.
 * Completed bootstrap commit refuses abort. Never delete immutable release/bin. */
bool setup_fresh_abort(L4Journal* journal,SetupFreshInstall* prepared,DWORD timeout);
ULONGLONG setup_fresh_bootstrap_sequence(const SetupFreshInstall* prepared);
ULONGLONG setup_fresh_receipt_sequence(const SetupFreshInstall* prepared);
/* Existing operation recovery is abort-only. Owner-trusted root must match the
 * saved admission, descriptor is authenticated again, inventory verified offline.
 * Never re-applies config, adopts new epochs or starts services on reload. */
bool setup_fresh_load_abort(L4Journal* journal,ULONGLONG receipt,
    const SetupRootManifest* root,SetupFreshInstall** prepared);
bool setup_fresh_saved_identity(L4Journal* journal,ULONGLONG receipt,L4CatalogRelease* selected);
/* Caller must wait for synchronous apply/abort before freeing this context. */
void setup_fresh_free(SetupFreshInstall* prepared);

/* Explicit elevated local setup enrollment/offline trust boundary. Remote callers
 * use the original strict APIs. No mutable process-wide trust flag exists. */
bool setup_fresh_prepare_broker_local(L4Journal* journal,const SetupRootManifest* root,
    const void* descriptor,DWORD descriptor_size,const BYTE* signature,DWORD signature_size,
    const wchar_t* archive,const L4BootstrapProfile profiles[4],const L4AccessActors* actors,
    const char* authenticated_sn,const SetupFreshConfig* extra,unsigned extra_count,SetupFreshInstall** prepared);
bool setup_fresh_load_abort_local(L4Journal* journal,ULONGLONG receipt,const SetupRootManifest* root,SetupFreshInstall** prepared);
