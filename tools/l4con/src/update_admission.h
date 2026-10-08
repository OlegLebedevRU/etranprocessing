#pragma once
#include "rpc_contract.h"
#include "../../l4common/remote_host.h"
typedef struct L4UpdateAdmission L4UpdateAdmission;
typedef void (*L4AdmissionReply)(void* context,const char* task,int code,const char* json);
/* Compile-time closed until real executor acceptance. No caller/env override. */
bool update_admission_enabled(void);
/* Owned adapter only; never tracks independent host execution or7032 observer. */
bool update_admission_busy(L4UpdateAdmission* admission);
bool update_admission_start(const L4Layout* roots,HANDLE stop,L4AdmissionReply reply,
    void* context,L4UpdateAdmission** output);
/* One pending/running task. Copies only original UUID, version and suite target.
 * ERROR_NOT_SUPPORTED is an explicit closed engine/updater refusal, never202. */
bool update_admission_submit(L4UpdateAdmission* admission,const RpcCommand* command);
bool update_admission_close(L4UpdateAdmission** admission,DWORD timeout);
