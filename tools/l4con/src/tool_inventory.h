#ifndef L4CON_TOOL_INVENTORY_H
#define L4CON_TOOL_INVENTORY_H

#include <stdbool.h>
#include <stddef.h>

/* A bounded JSON array of installed L4 Tools binaries, ordered by tool and path. */
bool tool_inventory_build(char *out, size_t capacity, char digest[65]);
bool tool_inventory_package_version(char out[64]);

#endif
