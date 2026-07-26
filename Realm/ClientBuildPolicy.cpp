/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * multiple client generations from one realmd process.
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 */

#include "ClientBuildPolicy.h"

namespace
{
ClientBuildPolicy const SupportedClientBuilds[] =
{
    {{40000, 9, 0, 0, ' '}, REALM_VERSION_SHADOWLANDS},
    {{35662, 8, 3, 7, ' '}, REALM_VERSION_BFA},
    {{26972, 7, 3, 5, ' '}, REALM_VERSION_LEGION},
    {{21742, 6, 2, 4, ' '}, REALM_VERSION_WOD},
    {{18414, 5, 4, 8, ' '}, REALM_VERSION_MOP},
    {{18273, 5, 4, 8, ' '}, REALM_VERSION_MOP},
    {{15595, 4, 3, 4, ' '}, REALM_VERSION_CATA},
    {{12340, 3, 3, 5, 'a'}, REALM_VERSION_WOTLK},
    {{8606, 2, 4, 3, ' '}, REALM_VERSION_TBC},
    {{6141, 1, 12, 3, ' '}, REALM_VERSION_VANILLA},
    {{6005, 1, 12, 2, ' '}, REALM_VERSION_VANILLA},
    {{5875, 1, 12, 1, ' '}, REALM_VERSION_VANILLA},
};
}

ClientBuildPolicy const* FindClientBuildPolicy(uint32 build)
{
    for (ClientBuildPolicy const& policy : SupportedClientBuilds)
    {
        if (static_cast<uint32>(policy.buildInfo.build) == build)
        {
            return &policy;
        }
    }

    return nullptr;
}

RealmBuildInfo const* FindBuildInfo(uint16 build)
{
    ClientBuildPolicy const* policy = FindClientBuildPolicy(build);
    return policy ? &policy->buildInfo : nullptr;
}
