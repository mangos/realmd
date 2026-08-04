/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

/** \file
 \ingroup realmd
 */

// Socket headers for AF_INET / in_addr / addrinfo. These used to arrive
// transitively through the ACE includes in the old Common.h; with that header
// gone they have to be named.
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#endif
#include "Common/ServerDefines.h"
#include "Platform/Define.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include "ClientBuildPolicy.h"
#include "RealmList.h"
#include "Auth/AuthCodes.h"
#include "Util.h"                                           // for Tokens typedef
#include "Policies/Singleton.h"
#include "Database/DatabaseEnv.h"


extern DatabaseType LoginDatabase;

/// Resolve a host string (dotted IPv4 or hostname) to a RealmAddress.
/// The socket headers (inet_pton/getaddrinfo/ntohl) come in via Common.h.
static RealmAddress ResolveRealmAddress(const std::string& host, uint16 port)
{
    RealmAddress result;
    result.port = port;

    uint32 hostOrderIp = 0;

    struct in_addr addr;
    if (inet_pton(AF_INET, host.c_str(), &addr) == 1)
    {
        hostOrderIp = ntohl(addr.s_addr);
    }
    else
    {
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* res = NULL;
        if (getaddrinfo(host.c_str(), NULL, &hints, &res) == 0 && res)
        {
            hostOrderIp = ntohl(reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr.s_addr);
            freeaddrinfo(res);
        }
        else
        {
            sLog.outError("Could not resolve realm address '%s'", host.c_str());
        }
    }

    result.ip = hostOrderIp;
    result.loopback = ((hostOrderIp >> 24) == 127);
    return result;
}

RealmList::RealmList()
{
}

RealmList& sRealmList
{
    static RealmList realmlist;
    return realmlist;
}

RealmVersion RealmList::BelongsToVersion(uint32 build) const
{
    ClientBuildPolicy const* policy = FindClientBuildPolicy(build);
    return policy ? policy->realmVersion : REALM_VERSION_VANILLA;
}

RealmListView RealmList::GetRealmsForBuild(uint32 build) const
{
    return RealmListView(m_snapshots.Load(), BelongsToVersion(build));
}

/// Load the realm list from the database
void RealmList::Initialize(uint32 updateInterval)
{
    ///- Get the content of the realmlist table in the database
    m_snapshots.Publish(BuildSnapshot(true));
    m_refreshGate.Reset(updateInterval, time(NULL));
}

void RealmList::AddRealmToBuildList(
    RealmSnapshot& snapshot, Realm const& realm)
{
    RealmBuilds builds = realm.realmbuilds;
    int buildNumber = *(builds.begin());
    snapshot.realmsByVersion[BelongsToVersion(buildNumber)].push_back(&realm);
}

void RealmList::UpdateRealm(
    RealmSnapshot& snapshot,
    uint32 ID,
    const std::string& name,
    RealmAddress const& address,
    RealmAddress const& localAddr,
    RealmAddress const& localSubmask,
    uint32 port,
    uint8 icon,
    RealmFlags realmflags,
    uint8 timezone,
    AccountTypes allowedSecurityLevel,
    float popu,
    const std::string& builds)
{
    ///- Create new if not exist or update existed
    Realm& realm = snapshot.realms[name];

    realm.m_ID       = ID;
    realm.name       = name;
    realm.icon       = icon;
    realm.realmflags = realmflags;
    realm.timezone   = timezone;
    realm.allowedSecurityLevel = allowedSecurityLevel;
    realm.populationLevel      = popu;

    Tokens tokens = StrSplit(builds, " ");
    Tokens::iterator iter;

    for (iter = tokens.begin(); iter != tokens.end(); ++iter)
    {
        uint32 build = uint32(std::strtoul((*iter).c_str(), NULL, 10));
        realm.realmbuilds.insert(build);
    }

    uint16 first_build = !realm.realmbuilds.empty() ? *realm.realmbuilds.begin() : 0;

    if (first_build)
    {
        AddRealmToBuildList(snapshot, realm);
    }
    else
    {
        sLog.outError("You don't seem to have added any allowed realmbuilds to the realm: %s"
            " and therefore it will not be listed to anyone", name.c_str());
    }
    realm.realmBuildInfo.build = first_build;
    realm.realmBuildInfo.major_version = 0;
    realm.realmBuildInfo.minor_version = 0;
    realm.realmBuildInfo.bugfix_version = 0;
    realm.realmBuildInfo.hotfix_version = ' ';

    if (first_build)
    {
        if (RealmBuildInfo const* bInfo = FindBuildInfo(first_build))
        {
            if (bInfo->build == first_build)
            {
                realm.realmBuildInfo = *bInfo;
            }
        }
    }

    ///- Append port to IP address.
    realm.ExternalAddress = address;
    realm.LocalAddress = localAddr;
    realm.LocalSubnetMask = localSubmask;
}

void RealmList::UpdateIfNeed()
{
    m_refreshGate.RunIfDue(time(NULL), [this]
    {
        m_snapshots.Publish(BuildSnapshot(false));
    });
}

std::shared_ptr<RealmSnapshot> RealmList::BuildSnapshot(bool init)
{
    DETAIL_LOG("Updating Realm List...");
    auto snapshot = std::make_shared<RealmSnapshot>();

    ////                                               0     1       2          3               4                  5       6       7             8           9                       10            11
    QueryResult* result = LoginDatabase.Query("SELECT `id`, `name`, `address`, `localAddress`, `localSubnetMask`, `port`, `icon`, `realmflags`, `timezone`, `allowedSecurityLevel`, `population`, `realmbuilds` FROM `realmlist` WHERE (`realmflags` & 1) = 0 ORDER BY `name`");

    ///- Circle through results and add them to the realm map
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            uint32 Id                       = fields[0].GetUInt32();
            std::string name                = fields[1].GetString();
            std::string externalAddress     = fields[2].GetString();
            std::string localAddress        = fields[3].GetString();
            std::string localSubmask        = fields[4].GetString();
            uint32 port                     = fields[5].GetUInt32();
            uint8 icon                      = fields[6].GetUInt8();
            uint8 realmflags                = fields[7].GetUInt8();
            uint8 timezone                  = fields[8].GetUInt8();
            uint8 allowedSecurityLevel      = fields[9].GetUInt8();
            float population                = fields[10].GetFloat();
            std::string realmbuilds         = fields[11].GetString();

            RealmAddress externalAddr = ResolveRealmAddress(externalAddress, port);
            RealmAddress localAddr = ResolveRealmAddress(localAddress, port);
            RealmAddress submask = ResolveRealmAddress(localSubmask, 0);

            if (realmflags & ~(REALM_FLAG_OFFLINE | REALM_FLAG_NEW_PLAYERS | REALM_FLAG_RECOMMENDED | REALM_FLAG_SPECIFYBUILD))
            {
                sLog.outError("Realm (id %u, name '%s') can only be flagged as OFFLINE (mask 0x02), NEWPLAYERS (mask 0x20), RECOMMENDED (mask 0x40), or SPECIFICBUILD (mask 0x04) in DB", Id, name.c_str());
                realmflags &= (REALM_FLAG_OFFLINE | REALM_FLAG_NEW_PLAYERS | REALM_FLAG_RECOMMENDED | REALM_FLAG_SPECIFYBUILD);
            }

            UpdateRealm(*snapshot, Id, name, externalAddr, localAddr, submask, port, icon, RealmFlags(realmflags), timezone, (allowedSecurityLevel <= SEC_ADMINISTRATOR ? AccountTypes(allowedSecurityLevel) : SEC_ADMINISTRATOR), population, realmbuilds);

            if (init)
            {
                sLog.outString("Added realm id %u, name '%s'",  Id, name.c_str());
            }
        }
        while (result->NextRow());
        delete result;
    }

    // Stamped after the query rather than before it: the age the console
    // reports is the age of the data, and a slow query is exactly when that
    // distinction matters.
    snapshot->publishedAt = time(NULL);

    return snapshot;
}
