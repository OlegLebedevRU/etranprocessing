#pragma once
#include "worker_handoff.h"
typedef bool (*L4HandoffAudit)(void*,L4Journal*,const L4RecoveryHelper*,DWORD);
bool l4_worker_transfer_checked(L4Journal**,L4WorkerJob*,const L4RecoveryHelper*,DWORD,L4HandoffAudit,void*);
bool l4_worker_accept_checked(const L4Layout*,const wchar_t*,DWORD,L4Journal**,L4HandoffAudit,void*);
bool l4_worker_recheck_checked(L4Journal*,L4WorkerAdmission*,L4HandoffAudit,void*);
bool l4_worker_recheck_active_checked(L4Journal*,const L4UpdateState*,L4WorkerAdmission*,L4HandoffAudit,void*);
