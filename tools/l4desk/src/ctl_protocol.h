#ifndef L4DESK_CTL_PROTOCOL_H
#define L4DESK_CTL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "desktop_state.h"

int64_t ctl_get_time_ms(void);
void ctl_get_utc_iso(char* out, size_t max_len);

int ctl_build_presence_payload(char* buf, size_t max_len,
                              const char* status,
                              bool desktop_available,
                              const ScreenMetrics* screen);

int ctl_build_ack_payload(char* buf, size_t max_len,
                          const char* command_id,
                          const char* lease_id,
                          const char* sn,
                          int64_t terminal_time_ms);

int ctl_build_nack_payload(char* buf, size_t max_len,
                           const char* command_id,
                           const char* lease_id,
                           const char* sn,
                           const char* code,
                           const char* message,
                           int64_t terminal_time_ms);

bool ctl_handle_command(const char* payload, size_t payload_len,
                        const char* own_sn,
                        char* out_resp, size_t max_resp,
                        size_t* out_resp_len,
                        bool* p_should_publish,
                        uint8_t* p_qos);

#endif /* L4DESK_CTL_PROTOCOL_H */
