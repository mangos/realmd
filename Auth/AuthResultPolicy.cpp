/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * multiple client generations from one realmd process.
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 */

#include "AuthResultPolicy.h"
#include "Realm/ClientBuildPolicy.h"

AuthResult LockedAccountResultForBuild(uint32 build)
{
    ClientBuildPolicy const* policy = FindClientBuildPolicy(build);
    if (!policy || policy->realmVersion == REALM_VERSION_VANILLA)
    {
        return WOW_FAIL_DB_BUSY;
    }

    return WOW_FAIL_LOCKED_ENFORCED;
}
