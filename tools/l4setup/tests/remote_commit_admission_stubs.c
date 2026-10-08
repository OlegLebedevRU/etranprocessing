/* Codec/storage-only fixture. These external admission adapters can never grant
 * target trust or terminal success; production builds use the real adapters. */
#include "../src/remote_commit.h"
#include "../../l4common/communication_plan.h"
static bool refuse(void){SetLastError(ERROR_CALL_NOT_IMPLEMENTED);return false;}
bool setup_update_load_operation_snapshot(const L4JournalReader* r,const L4Layout* l,ULONGLONG s,SetupOperationPlan** p){(void)r;(void)l;(void)s;*p=NULL;return refuse();}
void setup_operation_free(SetupOperationPlan* p){(void)p;}
unsigned setup_operation_hops(const SetupOperationPlan* p){(void)p;return 0;}
const SetupManifest* setup_operation_target(const SetupOperationPlan* p,unsigned h){(void)p;(void)h;return NULL;}
const SetupRootManifest* setup_operation_target_root(const SetupOperationPlan* p,unsigned h){(void)p;(void)h;return NULL;}
const BYTE* setup_root_identity(const SetupRootManifest* p){(void)p;return NULL;}
const L4RoutePlan* setup_operation_route(const SetupOperationPlan* p){(void)p;return NULL;}
const L4CatalogRoute* l4_route_steps(const L4RoutePlan* p){(void)p;return NULL;}
const char* l4_route_arch(const L4RoutePlan* p){(void)p;return NULL;}
const ULONGLONG* setup_operation_configs(const SetupOperationPlan* p,unsigned* n){(void)p;*n=0;return NULL;}
const L4ServiceSwitch* setup_operation_switch(const SetupOperationPlan* p,unsigned h,unsigned s){(void)p;(void)h;(void)s;return NULL;}
bool setup_manifest_verify(const SetupManifest* p){(void)p;return refuse();}
bool l4_communication_plan_decode(const L4Layout* l,const BYTE* b,DWORD n,L4CommunicationPlan* p){(void)l;(void)b;(void)n;(void)p;return refuse();}
bool l4_communication_decision_open(const L4Layout* l,const wchar_t* u,DWORD t,L4CommunicationDecision** p){(void)l;(void)u;(void)t;*p=NULL;return refuse();}
bool l4_communication_decision_read(L4CommunicationDecision* p,L4CommunicationPhase* s,DWORD* e){(void)p;(void)s;(void)e;return refuse();}
void l4_communication_decision_close(L4CommunicationDecision* p){(void)p;}

ULONGLONG setup_operation_switch_reference(const SetupOperationPlan* p,unsigned h,unsigned s){(void)p;(void)h;(void)s;return 0;}
