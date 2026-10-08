#pragma once
/* Public read-only Registry authority; not a credential or internal endpoint.
 * Keep producer config.toml authority aligned; no RPC/metadata host override. */
#define L4_REGISTRY_HOST "l4tools-generic.ar.cloud.ru"
#define L4_REGISTRY_HOST_W L"l4tools-generic.ar.cloud.ru"
#define L4_REGISTRY_AUTHORITY L4_REGISTRY_HOST ":443"
#define L4_REGISTRY_MAX_ARCHIVE (1024ULL*1024*1024)
