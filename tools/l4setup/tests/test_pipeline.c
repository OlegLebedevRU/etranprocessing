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
static bool read_only;
static int mutations;
static int rollback_stops,rollback_restores,rollback_starts;
static int upstream_calls;
static cert_state discovered_state;
static const char* discovered_version;
static InstallSummaryData written;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n",__LINE__,#c); errors++; } } while(0)
void log_info(const char* f, ...) {(void)f;}
void log_warn(const char* f, ...) {(void)f;}
void log_err(const char* f, ...) {(void)f;}
void log_get_path(wchar_t* p, size_t n) {wcscpy_s(p,n,L"test.log");}
bool preflight_check(PreflightInfo* p) {memset(p,0,sizeof(*p));return true;}
bool preflight_setup_firewall(const wchar_t* p) {(void)p;mutations++;return true;}
bool preflight_configure_win7_tls12(void) {mutations++;return true;}
bool install_root_ca_certificate_ex(const wchar_t* p) {(void)p;mutations++;return true;}
bool unpack_recover_from_crash(const wchar_t* p) {(void)p;mutations++;return true;}
bool unpack_read_installed_version(const wchar_t* p,char* v,size_t n) {(void)p;strcpy_s(v,n,read_only?discovered_version:fail==5?"1.9.4":"1.9.5");return true;}
bool unpack_is_idempotent(const wchar_t* p,const char* v) {(void)p;(void)v;return true;}
int version_compare(const char* a,const char* b) {return strcmp(a,b);}
cert_state cert_discover(const wchar_t* s, cert_info* i) {(void)s;memset(i,0,sizeof(*i));return read_only?discovered_state:cert_mode?CERT_ABSENT:CERT_VALID;}
const char* cert_state_to_str(cert_state s) {(void)s;return "absent";}
bool services_configure_environment(const wchar_t* p) {(void)p;mutations++;return true;}
bool drainage_execute(const wchar_t* p,const char* sn,bool silent,DrainageResult* r) {(void)p;(void)sn;(void)silent;memset(r,0,sizeof(*r));CHECK(stage==0);stage=1;return fail!=1;}
bool unpack_payload(const wchar_t* p,const char* arch,const wchar_t* dev,const char* v) {(void)p;(void)arch;(void)dev;(void)v;CHECK(stage==1);stage=2;return true;}
bool services_ensure_all_registered(const wchar_t* p) {(void)p;CHECK(stage==2);stage=3;return true;}
bool unpack_rollback(const wchar_t* p,const char* v) {(void)p;(void)v;CHECK(rollback_stops==1);rollback_restores++;return true;}
bool services_stop_all_in_order(const char* sn,ServiceLifecycleCallback cb,void* u) {(void)sn;(void)cb;(void)u;rollback_stops++;active=false;return true;}
bool cert_phase_execute(const wchar_t* p,CliOptions* o,HINSTANCE h,CertPhaseResult* r) {
 (void)p;(void)o;(void)h;mutations++;CHECK(stage==3 || stage==0);stage=4;
 r->state=cert_mode?CERT_ABSENT:CERT_VALID;r->exit_code=cert_mode;return true;
}
bool services_prepare_mosquitto(const wchar_t* p) {(void)p;mutations++;CHECK(stage==4);stage=5;prepared=fail!=2;return prepared;}
bool services_start_all_in_order(ServiceLifecycleCallback cb,void* u) {
 if(!cb){CHECK(rollback_restores==1);rollback_starts++;return true;}
 mutations++;CHECK(stage==5 && prepared);stage=6;
 cb(SVC_NAME_LEO4PROXY,SVC_STATUS_RUNNING,0,NULL,u);
 if(fail==3) {cb(SVC_NAME_MOSQUITTO,SVC_STATUS_FAILED,0,NULL,u);return false;}
 active=true;return true;
}
DWORD services_query_status(const wchar_t* s) {(void)s;return active?SERVICE_RUNNING:SERVICE_STOPPED;}
bool setup_proxy_probe(int port,bool require,int timeout) {
 CHECK((read_only || stage==6) && port==18443 && timeout>0);CHECK(require==(cert_mode==0));return active;
}
bool smoke_run_probes(const wchar_t* p,bool require,SmokeProbesResult* r) {
 (void)p;CHECK((read_only || stage==6) && active);CHECK(require==(cert_mode==0));stage=7;
 strcpy_s(r->proxy_info,16,"ok");strcpy_s(r->mosquitto_port,16,"ok");
 if(fail==4)r->critical_failed=true;return fail!=4;
}
bool unpack_clear_incomplete_marker(const wchar_t* p) {(void)p;mutations++;CHECK(stage==7);return true;}
bool state_patch_version(const wchar_t* p,const char* v,const wchar_t* s) {(void)p;(void)v;(void)s;mutations++;return fail!=5;}
void summary_add_warning(InstallSummaryData* d,const char* w) {(void)d;(void)w;}
bool summary_write_json(const InstallSummaryData* d,const wchar_t* p) {(void)p;written=*d;return true;}
static void run_case(int failure,int certificate,int expected,SetupOperationType op) {
 SetupContext ctx={0};CliOptions opts={0};wcscpy_s(opts.dest,MAX_PATH,L"D:\\isolated-test");
 ctx.opts=&opts;ctx.op_type=op;strcpy_s(ctx.target_version,32,"1.9.5");
 if(failure==8)opts.network_specified=true;
 strcpy_s(ctx.summary.installed_version,32,op==OP_INSTALL?"":"1.9.4");
 if(op==OP_UPGRADE)strcpy_s(ctx.installed_version,32,"1.9.4");
 rollback_stops=rollback_restores=rollback_starts=0;
 fail=failure;cert_mode=certificate;stage=0;active=false;prepared=false;upstream_calls=0;
 memset(&written,0,sizeof(written));result=engine_run_pipeline(&ctx);
 CHECK(result==expected && written.exit_code==expected);
 if(expected==24) {CHECK(stage==5 || stage==6);CHECK(!written.probes.mosquitto_port[0]);CHECK(!strcmp(written.service_l4con,"stopped"));}
 if(!expected || expected==10 || expected==11 || expected==12) {CHECK(stage==7);CHECK(!strcmp(written.installed_version,"1.9.5"));}
 else CHECK(strcmp(written.installed_version,"1.9.5")!=0);
 if(op==OP_UPGRADE && (failure==2 || failure==3 || failure==4)) {CHECK(rollback_starts==1);CHECK(!strcmp(written.rollback,"restored"));}
 if(expected==12) CHECK(!strcmp(written.error_reason,"upstream_tls_failed"));
 if(failure==7)CHECK(upstream_calls==2);
 if(failure==8)CHECK(ctx.op_type==OP_REPAIR);
}
int main(void) {
 run_case(0,0,0,OP_INSTALL);run_case(0,10,10,OP_INSTALL);run_case(0,11,11,OP_INSTALL);
 run_case(0,0,0,OP_REPAIR);run_case(0,0,0,OP_UPGRADE);run_case(0,0,0,OP_VERIFY);
 run_case(1,0,22,OP_INSTALL);run_case(2,0,24,OP_INSTALL);run_case(3,0,24,OP_INSTALL);
 run_case(2,0,24,OP_UPGRADE);run_case(3,0,24,OP_UPGRADE);run_case(4,0,27,OP_UPGRADE);
 run_case(4,0,27,OP_INSTALL);run_case(5,0,31,OP_UPGRADE);
 run_case(6,0,12,OP_INSTALL);
 run_case(7,0,0,OP_UPGRADE);
 run_case(8,0,0,OP_VERIFY);
 read_only=true;
 const cert_state states[]={CERT_VALID,CERT_EXPIRING,CERT_BROKEN,CERT_ABSENT,CERT_STORE_ERROR};
 const char* versions[]={"","1.9.5","9.9.9"};
 for(int certificate=0;certificate<5;certificate++) for(int running=0;running<2;running++) for(int version=0;version<3;version++) {
    SetupContext ctx={0};CliOptions opts={0};opts.smoke_only=true;wcscpy_s(opts.dest,MAX_PATH,L"Z:\\isolated-smoke");ctx.opts=&opts;
    discovered_state=states[certificate];discovered_version=versions[version];
    fail=0;cert_mode=certificate<2?0:10;active=running!=0;stage=0;mutations=0;
    CHECK(engine_phase_check(&ctx));CHECK(ctx.op_type==OP_VERIFY);
    int code=engine_run_pipeline(&ctx);CHECK(code==(!running?27:certificate==4?20:certificate>=2?10:0));
    CHECK(mutations==0);CHECK(!strcmp(written.installed_version,versions[version]));
    if(ctx.hMutex) {ReleaseMutex(ctx.hMutex);CloseHandle(ctx.hMutex);}
 }
 printf("Pipeline regression: 44 cases, %d failures\n",errors);return errors?1:0;
}

void services_set_network_options(const CliOptions* options) { (void)options; }

void smoke_probe_upstream(const wchar_t* dest, SmokeProbesResult* probes) {
 (void)dest;
 ++upstream_calls;
 if(fail==6) {strcpy_s(probes->upstream_tls[3],32,"cert_invalid");probes->has_warnings=true;probes->calculated_exit_code=12;}
 if(fail==7) {strcpy_s(probes->upstream_tls[3],32,upstream_calls==1?"probe_failed":"valid");probes->has_warnings=upstream_calls==1;probes->calculated_exit_code=upstream_calls==1?12:0;}
}
