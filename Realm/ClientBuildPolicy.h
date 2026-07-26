/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * multiple client generations from one realmd process.
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 */

#ifndef MANGOS_H_CLIENTBUILDPOLICY
#define MANGOS_H_CLIENTBUILDPOLICY

#include "RealmSnapshot.h"

struct ClientBuildPolicy
{
    RealmBuildInfo buildInfo;
    RealmVersion realmVersion;
};

ClientBuildPolicy const* FindClientBuildPolicy(uint32 build);
RealmBuildInfo const* FindBuildInfo(uint16 build);

#endif
