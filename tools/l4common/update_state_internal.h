#pragma once
#include "update_state.h"
#include "journal_internal.h"
#define L4_UPDATE_STATE_SIZE 112u
#define L4_RECORD_UPDATE_STATE_INTENT 65u
bool l4_update_state_encode(const L4UpdateState* state,BYTE bytes[L4_UPDATE_STATE_SIZE]);
bool l4_update_state_pin(const L4Layout* layout,L4FileFence* fence);
bool l4_update_state_replace(const L4Layout* layout,const BYTE bytes[L4_UPDATE_STATE_SIZE],bool create_only);
