#pragma once
#include "worker_job.h"
/* Isolated native fixtures use their own GUID/current token, no SYSTEM/CLI bypass
 * in public create/resume. Internal gate must audit task before returning true. */
bool l4_worker_job_create_id(const GUID* operation,L4WorkerJob** owner);
typedef bool (*L4WorkerArmCheck)(void*);
bool l4_worker_job_resume_checked(L4WorkerJob* owner,const L4RecoveryPlan* plan,L4WorkerArmCheck audit,void* context);
