#ifndef L4CON_FM_PROCESS_H
#define L4CON_FM_PROCESS_H
#include "file_manager.h"
int fm_worker_main(int argc,wchar_t** argv);
bool fm_child_start(const char*,int,HANDLE,FmResult,void*);
void fm_child_shutdown(void);
bool fm_child_enqueue(const RpcCommand*,bool);
bool fm_child_busy(void);
void fm_child_connection(bool);
void fm_child_tick(void);
void fm_child_set_navigation_result(FmNavigationResult);
bool fm_child_navigation(const char*,size_t,bool);
#endif
