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

/// \addtogroup realmd
/// @{
/// \file

#ifndef MANGOS_H_REALMLIST
#define MANGOS_H_REALMLIST

#include "RealmSnapshot.h"

#include <memory>
#include <string>
#include <map>

/**
 * @brief Storage object for the list of realms on the server
 *
 */
class RealmList
{
    public:
        /**
         * @brief
         *
         */
        typedef std::map<uint32, RealmVersion> RealmBuildVersionMap;

        static RealmList& Instance();

        RealmList();
        ~RealmList() {};

        void Initialize(uint32 updateInterval);

        /**
         * Initializes a map holding a link from build number to a version.
         * \see RealmVersion
         */
        void InitVersionToBuild();

        void UpdateIfNeed();

        RealmListView GetRealmsForBuild(uint32 build) const;

        /**
         * @return the total number of realms available
         */
        uint32 size() const
        {
            return static_cast<uint32>(m_snapshots.Load()->realms.size());
        }
    private:
        /**
         * Checks what version (ie, vanilla, tbc) a certain build number belongs to
         * @param build the build you want to check the version for
         * @return the corresponding version to the given build number
         */
        RealmVersion BelongsToVersion(uint32 build) const;

        /**
         * Adds entries to a map containing a link from a build number to a certain
         * wow version, ie: \ref RealmVersion::REALM_VERSION_VANILLA.
         * \see RealmVersion
         */
        void InitBuildToVersion();

        /**
         * Adds the given \ref Realm to a list sorted by version, ie: vanilla, tbc etc. This
         * in turn is used to only present the compatible realms to the clients connecting,
         * ie: vanilla clients will only see vanilla realms.
         *
         * This is controlled by what you set in the allowedbuilds field in the realm.realmlist
         * database, if you set more than one build the first one found in there will be
         * used, so if you tag a realm as this: "8606 6141" only TBC clients will be able to
         * see the realm and connect to it.
         * @param realm the realm you want to add to the sorted list, should be done for all realms
         * \see RealmVersion
         */
        void AddRealmToBuildList(RealmSnapshot& snapshot, Realm const& realm);

        std::shared_ptr<RealmSnapshot> BuildSnapshot(bool init);

        /**
         * @brief
         *
         * @param ID
         * @param name
         * @param address
         * @param port
         * @param icon
         * @param realmflags
         * @param timezone
         * @param allowedSecurityLevel
         * @param popu
         * @param builds
         */
        void UpdateRealm(RealmSnapshot& snapshot, uint32 ID, const std::string& name, RealmAddress const& address, RealmAddress const& localAddress, RealmAddress const& localSubnetmask, uint32 port, uint8 icon, RealmFlags realmflags, uint8 timezone, AccountTypes allowedSecurityLevel, float popu, const std::string& builds);
    private:
        RealmSnapshotStore m_snapshots;
        RealmRefreshGate m_refreshGate;
        RealmBuildVersionMap m_buildToVersion;
};

#define sRealmList RealmList::Instance()

#endif
/// @}
