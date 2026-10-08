#pragma once
#include "layout.h"
/* Existing independent monotonic security metadata only. Read-only held
 * private file/ancestry, compiled owner key ID and complete canonical chain;
 * absence is allowed. Never creates, truncates, repairs or resets a floor.
 * Does not admit a release, compatibility edge or mutable configuration. */
bool l4_catalog_floor_verify_existing(const L4Layout* layout);
