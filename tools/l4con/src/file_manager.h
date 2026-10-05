#ifndef L4CON_FILE_MANAGER_H
#define L4CON_FILE_MANAGER_H
#include "rpc_contract.h"
#include <windows.h>
typedef void (*FmResult)(void* context,const char* task_id,int status,const char* code);
bool fm_start(const char* sn,HANDLE stop,FmResult result,void* context);
void fm_shutdown(void);
bool fm_enqueue(const RpcCommand* command,bool console_busy);
bool fm_busy(void);
void fm_connection(bool connected);
void fm_tick(void);
#endif
