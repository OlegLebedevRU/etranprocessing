#pragma once
#include "catalog.h"
typedef struct {unsigned from,to;char arch[4],profile[64];BYTE evidence[32];} L4CatalogTransition;
struct L4Catalog {
    ULONGLONG revision,issued,expires;BYTE digest[32];char key_id[65];int stable;
    unsigned release_count,transition_count;
    L4CatalogRelease releases[L4_CATALOG_MAX_RELEASES];
    L4CatalogTransition transitions[L4_CATALOG_MAX_TRANSITIONS];
};
