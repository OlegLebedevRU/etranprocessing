#include "../src/fresh_install.h"
#include "../src/install_path.h"
#include "../../l4common/launcher.h"
#include "../../l4common/journal_internal.h"
#include "../../l4common/catalog_floor.h"
#include "../../l4common/tests/update_state_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions,failures,packages,acls,registered,activated,commits,aborts,launcher_calls;
static unsigned fault;static bool service_present;static UpdateFixture fixture;static L4ReleaseFile inventory_files[13];static ULONGLONG path_sequence;
static unsigned floor_checks;
static bool fake_floor_verify(const L4Layout* roots){(void)roots;floor_checks++;if(fault==96){SetLastError(ERROR_INVALID_DATA);return false;}return true;}
#define CHECK(x) do{++assertions;if(!(x)){++failures;printf("FAIL fresh %u: %s (%lu)\n",__LINE__,#x,GetLastError());}}while(0)
static bool fixture_write(const wchar_t* path,const char* bytes,DWORD size){HANDLE h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);if(h==INVALID_HANDLE_VALUE)return false;DWORD written=0;
    bool ok=WriteFile(h,bytes,size,&written,NULL) && written==size && FlushFileBuffers(h);CloseHandle(h);return ok;}
static bool fake_root(const SetupRootManifest* root,const void* bytes,DWORD size,const BYTE* sig,DWORD sig_size,const L4Layout* layout,SetupManifest** result){
    CHECK(root && bytes && size && sig && sig_size && layout);*result=NULL;if(fault==1){SetLastError(ERROR_ACCESS_DENIED);return false;}*result=(SetupManifest*)2;return true;}
static void fake_free(SetupManifest* m){(void)m;}
static const L4ReleaseFile* fake_files(const SetupManifest* m,unsigned* count){CHECK(m);*count=13;return inventory_files;}
static bool fake_config(const SetupManifest* m,unsigned index,wchar_t source[MAX_PATH],wchar_t target[MAX_PATH]){
    CHECK(m && index<3);const wchar_t* targets[]={L"config\\l4superv.json",L"config\\mosquitto\\acl.conf",L"config\\l4capture\\idle_refresh.ini"};
    return l4_layout_component(&fixture.layout,inventory_files[10+index].component,inventory_files[10+index].file,source) && l4_layout_data_path(&fixture.layout,targets[index],target);}
static bool fake_package(const SetupManifest* m,const wchar_t* archive){CHECK(m && archive);++packages;if(fault==2){SetLastError(ERROR_CRC);return false;}
    wchar_t expanded[MAX_PATH];CHECK(l4_layout_data_path(&fixture.layout,L"update\\staging\\fresh",expanded));
    for(unsigned i=0;i<13;i++){
        wchar_t relative[MAX_PATH],path[MAX_PATH];swprintf_s(relative,MAX_PATH,L"update\\staging\\fresh\\%ls",inventory_files[i].component);
        const wchar_t* slash=wcsrchr(inventory_files[i].file,L'\\');if(slash){wcscat_s(relative,MAX_PATH,L"\\");wcsncat_s(relative,MAX_PATH,inventory_files[i].file,(size_t)(slash-inventory_files[i].file));}
        if(!l4_layout_prepare_leaf(&fixture.layout,relative,NULL,NULL,false))return false;
        swprintf_s(path,MAX_PATH,L"%ls\\%ls\\%ls",expanded,inventory_files[i].component,inventory_files[i].file);if(!fixture_write(path,"exe",3))return false;
    }
    wchar_t zip[MAX_PATH];BYTE digest[32];if(!l4_layout_data_path(&fixture.layout,L"update\\cache\\fresh.zip",zip) || !fixture_write(zip,"zip",3) || !l4_store_hash("zip",3,NULL,0,digest))return false;
    return l4_release_publish(&fixture.layout,zip,digest,expanded,inventory_files,13);
}
static bool fake_verify(const SetupManifest* m){CHECK(m);if(fault==3){SetLastError(ERROR_CRC);return false;}return l4_release_verify(&fixture.layout,inventory_files,13);}
static bool fake_package_policy(const SetupManifest* m,const wchar_t* path,SetupAdmissionPolicy policy){CHECK(policy==SETUP_ADMISSION_STRICT_REMOTE || policy==SETUP_ADMISSION_LOCAL_OFFLINE);return fake_package(m,path);}
static bool fake_verify_policy(const SetupManifest* m,SetupAdmissionPolicy policy){CHECK(policy==SETUP_ADMISSION_STRICT_REMOTE || policy==SETUP_ADMISSION_LOCAL_OFFLINE);return fake_verify(m);}
static bool fake_sid(PSID sid,WELL_KNOWN_SID_TYPE type){(void)sid;CHECK(type==WinLocalSystemSid);return fault!=4;}
static bool fake_inventory(const wchar_t* name,L4ServiceInventory* s){CHECK(name);memset(s,0,sizeof(*s));s->installed=service_present || fault==5;return true;}
static bool fake_acl_prepare(const L4Layout* layout,const L4AccessActors* actors){++acls;CHECK(packages==1);if(fault==6){SetLastError(ERROR_ACCESS_DENIED);return false;}return l4_access_prepare(layout,actors);}
static bool fake_launcher_install(L4Journal* j,const L4Layout* layout,const wchar_t* tool,const L4ReleaseFile* files,unsigned count){
    ++launcher_calls;if(fault==7 && launcher_calls==3){SetLastError(ERROR_WRITE_FAULT);return false;}return l4_launcher_install(j,layout,tool,files,count);}
static bool fake_register(L4Journal* j,ULONGLONG sequence){L4BootstrapPlan plan;CHECK(l4_bootstrap_load(j,sequence,&plan));++registered;service_present=true;return fault!=8;}
static bool fake_activate(L4Journal* j,ULONGLONG sequence,const L4BootstrapChecks* gates){CHECK(j && sequence && gates);++activated;return fault!=9;}
static bool fake_commit(L4Journal* j,ULONGLONG sequence,const L4BootstrapChecks* gates,DWORD timeout){CHECK(j && sequence && gates && timeout);++commits;if(fault==10)return false;return true;}
static bool fake_local_commit(L4Journal* j,ULONGLONG sequence,DWORD timeout){CHECK(j && sequence && timeout && !activated);++commits;return true;}
static bool fake_abort(L4Journal* j,ULONGLONG sequence,DWORD timeout){CHECK(j && sequence && timeout);++aborts;if(fault==11 || commits && fault!=10){SetLastError(ERROR_CANCELLED);return false;}service_present=false;return true;}
static bool fake_path_prepare(L4Journal* j,SetupInstallPath** p){CHECK(j && !registered);if(fault==21){SetLastError(ERROR_ACCESS_DENIED);return false;}CHECK(l4_journal_append(j,80,"path",4,&path_sequence));*p=(SetupInstallPath*)3;return true;}
static const BYTE* fake_root_identity(const SetupRootManifest* root){CHECK(root);static const BYTE digest[32]={1};return digest;}
static ULONGLONG fake_path_sequence(const SetupInstallPath* p){CHECK(p==(SetupInstallPath*)3);return path_sequence;}
static bool fake_path_load(L4Journal* j,ULONGLONG sequence,SetupInstallPath** p){CHECK(j && sequence==path_sequence);*p=(SetupInstallPath*)3;return true;}
static bool fake_path_apply(L4Journal* j,SetupInstallPath* p){CHECK(j && p && (activated || fault==24) && !commits);return fault!=22 && fault!=23;}
static bool fake_path_rollback(L4Journal* j,SetupInstallPath* p){CHECK(j && p && aborts && !service_present);return fault!=23;}
static void fake_path_free(SetupInstallPath* p){CHECK(!p || p==(SetupInstallPath*)3);}
static bool fake_broker_environment(L4Journal* j,ULONGLONG sequence){CHECK(j && sequence && registered && !activated);return true;}
#define l4_service_inventory fake_inventory
#define fail bootstrap_fail
#include "../../l4common/bootstrap.c"
#undef fail
#include "../../l4common/journal_codec.c"
#define setup_root_descriptor_trusted fake_root
#define setup_manifest_free fake_free
#define setup_manifest_files fake_files
#define setup_manifest_config fake_config
#define setup_manifest_prepare fake_package
#define setup_manifest_verify fake_verify
#define setup_manifest_prepare_policy fake_package_policy
#define setup_manifest_verify_policy fake_verify_policy
#define IsWellKnownSid fake_sid
#define l4_access_prepare fake_acl_prepare
#define l4_launcher_install fake_launcher_install
#define l4_bootstrap_register fake_register
#define l4_bootstrap_activate fake_activate
#define l4_bootstrap_commit fake_commit
#define l4_bootstrap_local_commit fake_local_commit
#define l4_bootstrap_abort fake_abort
#define setup_path_prepare fake_path_prepare
#define setup_path_apply fake_path_apply
#define setup_path_rollback fake_path_rollback
#define setup_path_free fake_path_free
#define setup_path_sequence fake_path_sequence
#define setup_path_load fake_path_load
#define setup_root_identity fake_root_identity
#define setup_broker_environment_prepare fake_broker_environment
#define l4_catalog_floor_verify_existing fake_floor_verify
#include "../src/fresh_install.c"
/* Actual fresh-directory enumeration; the floor's cryptographic/security gate
 * is modeled here and separately exercised with real I/O by test_catalog_floor. */
static void retained_floor_test(void){
    fault=0;floor_checks=0;CHECK(update_fixture_init(&fixture));CHECK(data_empty(&fixture.layout));wchar_t path[MAX_PATH],other[MAX_PATH];
    CHECK(l4_layout_data_path(&fixture.layout,L"state\\catalog.floor",path));CHECK(fixture_write(path,"floor",5));CHECK(data_empty(&fixture.layout));CHECK(floor_checks==1);
    fault=96;CHECK(!data_empty(&fixture.layout));fault=0;CHECK(l4_layout_data_path(&fixture.layout,L"state\\actor-config.json",other));CHECK(fixture_write(other,"old",3));CHECK(!data_empty(&fixture.layout));CHECK(DeleteFileW(other));CHECK(data_empty(&fixture.layout));
    CHECK(DeleteFileW(path));CHECK(CreateDirectoryW(path,NULL));CHECK(!data_empty(&fixture.layout));CHECK(RemoveDirectoryW(path));CHECK(data_empty(&fixture.layout));CHECK(update_fixture_dispose(&fixture));
}
static bool unused_probe(const L4BootstrapPlan* p,unsigned i,DWORD ms,void* c){(void)p;(void)i;(void)ms;(void)c;return false;}
static bool unused_barrier(const L4BootstrapPlan* p,DWORD ms,void* c){(void)p;(void)ms;(void)c;return false;}
int wmain(void){
    retained_floor_test();
    const wchar_t* components[]={L"leo4proxy",L"mosquitto",L"l4con",L"l4superv",L"l4desk",L"l4sql",L"l4pin",L"l4capture",L"ffmpeg",L"l4launch",L"templates",L"templates",L"templates"};
    const wchar_t* files[]={L"leo4proxy.exe",L"mosquitto.exe",L"l4con.exe",L"l4superv.exe",L"l4desk.exe",L"l4sql.exe",L"l4pin.exe",L"bin\\l4capture.exe",L"ffmpeg.exe",L"l4launch.exe",L"l4superv.json",L"mosquitto\\acl.conf",L"l4capture\\idle_refresh.ini"};
    for(unsigned i=0;i<13;i++){inventory_files[i].component=components[i];inventory_files[i].file=files[i];inventory_files[i].size=3;CHECK(l4_store_hash("exe",3,NULL,0,inventory_files[i].sha256));}
    HANDLE primary=NULL,token=NULL;CHECK(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY|TOKEN_DUPLICATE,&primary));
    CHECK(primary && DuplicateTokenEx(primary,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_IMPERSONATE,NULL,SecurityImpersonation,TokenImpersonation,&token));if(!token)return 1;
    L4AccessActors actors={token,token,token,token,token};L4BootstrapProfile profiles[4];const wchar_t* names[]={L"Leo4Proxy",L"mosquitto",L"L4Con",L"L4Superv"};
    for(unsigned i=0;i<4;i++)profiles[i]=(L4BootstrapProfile){names[i],L"LocalSystem",L"--service",SERVICE_AUTO_START};
    L4BootstrapChecks gates={unused_probe,unused_barrier,NULL,{1000,300000,1000,1000},1000};SetupFreshConfig config={L"mosquitto\\mosquitto.conf","broker",6};
    for(unsigned scenario=0;scenario<=24;scenario++){
        fault=scenario;packages=acls=registered=activated=commits=aborts=launcher_calls=0;service_present=false;CHECK(update_fixture_init(&fixture));
        L4Journal* j=NULL;CHECK(l4_journal_open(&fixture.layout,L"17730000-0000-4000-8000-000000000008",true,&j));if(!j)return 1;
        wchar_t target[MAX_PATH];CHECK(l4_layout_data_path(&fixture.layout,L"config\\mosquitto\\mosquitto.conf",target));
        if(scenario==12){wchar_t path[MAX_PATH];CHECK(l4_layout_data_path(&fixture.layout,L"state\\existing.txt",path));CHECK(fixture_write(path,"old",3));}
        if(scenario==13)CHECK(l4_journal_append(j,99,"foreign",7,NULL));
        if(scenario==18){wchar_t path[MAX_PATH];CHECK(l4_layout_data_path(&fixture.layout,L"update\\operations\\update.state",path));CHECK(fixture_write(path,"old",3));}
        SetupFreshInstall* p=NULL;SetupFreshConfig selected=config;if(scenario==14)selected.relative=L"..\\outside";if(scenario==15)selected.relative=L"leo4proxy\\service-args.txt";
        L4AccessActors selected_actors=actors;if(scenario==17)selected_actors.proxy=primary;
        SetupFreshConfig proposals[]={selected,selected};
        bool prepared=(scenario==0 || scenario==24)?(scenario==24?setup_fresh_prepare_broker_local:setup_fresh_prepare_broker)(j,(SetupRootManifest*)1,"d",1,(BYTE*)"s",1,L"protected.zip",profiles,&selected_actors,scenario==24?NULL:"Synthetic773",NULL,0,&p):
            setup_fresh_prepare(j,(SetupRootManifest*)1,"d",1,(BYTE*)"s",1,L"protected.zip",profiles,&selected_actors,proposals,scenario==19?2:1,&p);
        bool expected=scenario==24 || scenario==0 || scenario==7 || scenario==8 || scenario==9 || scenario==10 || scenario==11 || scenario==16 || scenario==20 || scenario==22 || scenario==23;
        CHECK(prepared==expected);CHECK(!registered && !activated && !commits);
        if(!prepared){CHECK(!p);CHECK(!acls || scenario==6 || scenario==3 || scenario==21);CHECK(GetFileAttributesW(target)==INVALID_FILE_ATTRIBUTES);}
        else{
            CHECK(p && p->count==13 && setup_fresh_bootstrap_sequence(p)>0);CHECK(GetFileAttributesW(target)==INVALID_FILE_ATTRIBUTES);
            if(scenario==16)CHECK(fixture_write(target,"operator",8));
            if(scenario==20)j->header[0]^=1;
            if(scenario==24){CHECK(p->admission==SETUP_ADMISSION_LOCAL_OFFLINE);CHECK(!setup_fresh_apply(j,p,&gates,1000) && !registered && !activated && !p->spent);}
            bool applied=scenario==24?setup_fresh_deploy_local(j,p,1000):setup_fresh_apply(j,p,&gates,1000);CHECK(applied==(scenario==0 || scenario==11 || scenario==24));
            if(scenario==24)CHECK(commits==1 && !activated);
            unsigned previous=registered;CHECK(!setup_fresh_apply(j,p,&gates,1000) && registered==previous);if(scenario==20)j->header[0]^=1;
            if(scenario==0 || scenario==11 || scenario==24){CHECK(!setup_fresh_abort(j,p,1000));CHECK(GetFileAttributesW(target)!=INVALID_FILE_ATTRIBUTES);}
            else if(scenario==16){CHECK(!registered);}
            else if(scenario==23){CHECK(!setup_fresh_abort(j,p,1000));CHECK(!service_present);CHECK(GetFileAttributesW(target)!=INVALID_FILE_ATTRIBUTES);}
            else{ULONGLONG saved=setup_fresh_receipt_sequence(p);L4CatalogRelease identity;CHECK(setup_fresh_saved_identity(j,saved,&identity) && identity.manifest_sha256[0]==1);
                setup_fresh_free(p);p=NULL;l4_journal_close(j);j=NULL;CHECK(l4_journal_open(&fixture.layout,L"17730000-0000-4000-8000-000000000008",false,&j));
                CHECK(setup_fresh_load_abort(j,saved,(SetupRootManifest*)2,&p));if(!p)return 1;CHECK(!setup_fresh_apply(j,p,&gates,1000));
                CHECK(setup_fresh_abort(j,p,1000));CHECK(GetFileAttributesW(target)==INVALID_FILE_ATTRIBUTES);CHECK(!service_present);}
            setup_fresh_free(p);
        }
        l4_journal_close(j);CHECK(update_fixture_dispose(&fixture));
    }
    CloseHandle(primary);CloseHandle(token);printf("Fresh install composition: %u checks, %u failures; real files/ACL/journal/templates/launchers; modeled admission/SYSTEM/SCM/barrier; no live install\n",assertions,failures);return failures?1:0;
}
