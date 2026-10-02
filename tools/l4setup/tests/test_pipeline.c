/* Runs the real installer engine with isolated collaborators: no SCM,
 * registry writes, certificate enrollment or changes to C:\l4tools. */
#include "engine.h"
#include "preflight.h"
#include "unpack.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
static int stage, fail, cert_mode, result, errors;
static bool active, prepared;
static InstallSummaryData written;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n",__LINE__,#c); errors++; } } while(0)
void log_info(const char* f, ...) {(void)f;}
void log_warn(const char* f, ...) {(void)f;}
void log_err(const char* f, ...) {(void)f;}
void log_get_path(wchar_t* p, size_t n) {wcscpy_s(p,n,L"test.log");}
bool preflight_check(PreflightInfo* p) {memset(p,0,sizeof(*p));return true;}
bool preflight_setup_firewall(const wchar_t* p) {(void)p;return true;}
bool preflight_configure_win7_tls12(void) {return true;}
bool install_root_ca_certificate_ex(const wchar_t* p) {(void)p;return true;}
bool unpack_recover_from_crash(const wchar_t* p) {(void)p;return true;}
bool unpack_read_installed_version(const wchar_t* p,char* v,size_t n) {(void)p;strcpy_s(v,n,fail==5?"1.9.4":"1.9.5");return true;}
bool unpack_is_idempotent(const wchar_t* p,const char* v) {(void)p;(void)v;return true;}
int version_compare(const char* a,const char* b) {return strcmp(a,b);}
cert_state cert_discover(const wchar_t* s, cert_info* i) {(void)s;memset(i,0,sizeof(*i));return CERT_ABSENT;}
const char* cert_state_to_str(cert_state s) {(void)s;return "absent";}
bool services_configure_environment(const wchar_t* p) {(void)p;return true;}
bool drainage_execute(const wchar_t* p,const char* sn,bool silent,DrainageResult* r) {(void)p;(void)sn;(void)silent;memset(r,0,sizeof(*r));CHECK(stage==0);stage=1;return fail!=1;}
bool unpack_payload(const wchar_t* p,const char* arch,const wchar_t* dev,const char* v) {(void)p;(void)arch;(void)dev;(void)v;CHECK(stage==1);stage=2;return true;}
bool services_ensure_all_registered(const wchar_t* p) {(void)p;CHECK(stage==2);stage=3;return true;}
bool unpack_rollback(const wchar_t* p,const char* v) {(void)p;(void)v;return true;}
bool cert_phase_execute(const wchar_t* p,CliOptions* o,HINSTANCE h,CertPhaseResult* r) {
 (void)p;(void)o;(void)h;CHECK(stage==3 || stage==0);stage=4;
 r->state=cert_mode?CERT_ABSENT:CERT_VALID;r->exit_code=cert_mode;return true;
}
bool services_prepare_mosquitto(const wchar_t* p) {(void)p;CHECK(stage==4);stage=5;prepared=fail!=2;return prepared;}
bool services_start_all_in_order(ServiceLifecycleCallback cb,void* u) {
 CHECK(stage==5 && prepared);stage=6;
 cb(SVC_NAME_LEO4PROXY,SVC_STATUS_RUNNING,0,NULL,u);
 if(fail==3) {cb(SVC_NAME_MOSQUITTO,SVC_STATUS_FAILED,0,NULL,u);return false;}
 active=true;return true;
}
DWORD services_query_status(const wchar_t* s) {(void)s;return active?SERVICE_RUNNING:SERVICE_STOPPED;}
bool setup_proxy_probe(int port,bool require,int timeout) {
 CHECK(stage==6 && port==18443 && timeout>0);CHECK(require==(cert_mode==0));return true;
}
bool smoke_run_probes(const wchar_t* p,bool require,SmokeProbesResult* r) {
 (void)p;CHECK(stage==6 && active);CHECK(require==(cert_mode==0));stage=7;
 strcpy_s(r->proxy_info,16,"ok");strcpy_s(r->mosquitto_port,16,"ok");
 if(fail==4)r->critical_failed=true;return fail!=4;
}
bool unpack_clear_incomplete_marker(const wchar_t* p) {(void)p;CHECK(stage==7);return true;}
bool state_patch_version(const wchar_t* p,const char* v,const wchar_t* s) {(void)p;(void)v;(void)s;return fail!=5;}
void summary_add_warning(InstallSummaryData* d,const char* w) {(void)d;(void)w;}
bool summary_write_json(const InstallSummaryData* d,const wchar_t* p) {(void)p;written=*d;return true;}
static void run_case(int failure,int certificate,int expected,SetupOperationType op) {
 SetupContext ctx={0};CliOptions opts={0};wcscpy_s(opts.dest,MAX_PATH,L"D:\\isolated-test");
 ctx.opts=&opts;ctx.op_type=op;strcpy_s(ctx.target_version,32,"1.9.5");
 strcpy_s(ctx.summary.installed_version,32,op==OP_INSTALL?"":"1.9.4");
 fail=failure;cert_mode=certificate;stage=0;active=false;prepared=false;
 memset(&written,0,sizeof(written));result=engine_run_pipeline(&ctx);
 CHECK(result==expected && written.exit_code==expected);
 if(expected==24) {CHECK(stage==5 || stage==6);CHECK(!written.probes.mosquitto_port[0]);CHECK(!strcmp(written.service_l4con,"stopped"));}
 if(!expected || expected==10 || expected==11) {CHECK(stage==7);CHECK(!strcmp(written.installed_version,"1.9.5"));}
 else CHECK(strcmp(written.installed_version,"1.9.5")!=0);
}
int main(void) {
 run_case(0,0,0,OP_INSTALL);run_case(0,10,10,OP_INSTALL);run_case(0,11,11,OP_INSTALL);
 run_case(0,0,0,OP_REPAIR);run_case(0,0,0,OP_UPGRADE);run_case(0,0,0,OP_VERIFY);
 run_case(1,0,22,OP_INSTALL);run_case(2,0,24,OP_INSTALL);run_case(3,0,24,OP_INSTALL);
 run_case(4,0,27,OP_INSTALL);run_case(5,0,31,OP_UPGRADE);
 printf("Pipeline regression: 11 cases, %d failures\n",errors);return errors?1:0;
}
