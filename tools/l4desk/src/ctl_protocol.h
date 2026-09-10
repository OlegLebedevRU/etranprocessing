#ifndef L4DESK_CTL_PROTOCOL_H
#define L4DESK_CTL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "desktop_state.h"
#include "display_inventory.h"
#include "ffmpeg_supervisor.h"

int64_t ctl_get_time_ms(void);
void ctl_get_utc_iso(char* out, size_t max_len);

int ctl_build_presence_payload(char* buf, size_t max_len,
                              const char* status,
                              bool desktop_available,
                              const ScreenMetrics* screen);

int ctl_build_extended_presence_payload(char* buf, size_t max_len,
                                       const char* status,
                                       bool desktop_available,
                                       const ScreenMetrics* screen,
                                       const SystemInventory* inv,
                                       const StreamStateInfo* stream);

int ctl_build_stream_event_payload(char* buf, size_t max_len,
                                  const char* sn,
                                  const char* stream_instance_id,
                                  const char* state,
                                  const char* reason);

int ctl_build_ack_payload(char* buf, size_t max_len,
                          const char* command_id,
                          const char* lease_id,
                          const char* sn,
                          int64_t terminal_time_ms);

int ctl_build_ack_stream_payload(char* buf, size_t max_len,
                                 const char* command_id,
                                 const char* lease_id,
                                 const char* sn,
                                 const char* result,
                                 const char* stream_instance_id,
                                 const char* state,
                                 int64_t terminal_time_ms);

int ctl_build_ack_inventory_payload(char* buf, size_t max_len,
                                    const char* command_id,
                                    const char* lease_id,
                                    const char* sn,
                                    const SystemInventory* inv,
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
                        const SystemInventory* inv,
                        char* out_resp, size_t max_resp,
                        size_t* out_resp_len,
                        bool* p_should_publish,
                        uint8_t* p_qos);

#endif /* L4DESK_CTL_PROTOCOL_H */
