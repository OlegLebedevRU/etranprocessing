#pragma once
#include "installed_source.h"
typedef struct SetupUpdaterIdentity SetupUpdaterIdentity;
/* Authenticated original fresh installer identity, independent of active suite.
 * Owns immutable executable/ancestor pins; never a mutable active-marker hint. */
bool setup_updater_identity_from_source(SetupInstalledSource* source,SetupUpdaterIdentity** identity);
bool setup_updater_identity_verify(SetupUpdaterIdentity* identity);
void setup_updater_identity_free(SetupUpdaterIdentity* identity);
const char* setup_updater_identity_version(const SetupUpdaterIdentity* identity);
const char* setup_updater_identity_arch(const SetupUpdaterIdentity* identity);
const BYTE* setup_updater_identity_root(const SetupUpdaterIdentity* identity);
const BYTE* setup_updater_identity_publisher(const SetupUpdaterIdentity* identity);
const SetupRootAsset* setup_updater_identity_asset(const SetupUpdaterIdentity* identity);
const wchar_t* setup_updater_identity_path(const SetupUpdaterIdentity* identity);
const wchar_t* setup_updater_identity_origin(const SetupUpdaterIdentity* identity);
