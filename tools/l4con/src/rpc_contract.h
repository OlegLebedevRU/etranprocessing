#ifndef L4CON_RPC_CONTRACT_H
#define L4CON_RPC_CONTRACT_H
#include "command_runner.h"
typedef struct {
    int method;
    char task_id[40];
    bool payload_required;
    bool empty;
    char session_id[64];
    char command_line[L4CON_COMMAND_UTF8_CAP];
    char command_id[128];
    char shell[32];
    char pin[7];
    unsigned long long pin_expires_at;
    char fm_action[16];
    char fm_operation_id[40];
    unsigned long long fm_expires_at;
    int ttl_sec;
    int max_output_bytes;
} RpcCommand;
bool rpc_contract_parse(const char* body, size_t length, bool announcement,
                        const char* wire_method, const char* wire_correlation,
                        const char* wire_payload_required, RpcCommand* out);
bool rpc_uuid(const char* value);
#endif
