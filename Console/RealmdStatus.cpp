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

#include "Console/RealmdStatus.h"

namespace MaNGOS::Realmd
{
    void ApplyRealmSnapshot(
        RealmSnapshot const& snapshot, time_t now, RealmdStatus& status)
    {
        status.realmsTotal = static_cast<uint32>(snapshot.realms.size());
        status.realmsOnline = 0;

        for (RealmMap::const_iterator itr = snapshot.realms.begin();
             itr != snapshot.realms.end(); ++itr)
        {
            if ((itr->second.realmflags & REALM_FLAG_OFFLINE) == 0)
            {
                ++status.realmsOnline;
            }
        }

        status.snapshotPublished = (snapshot.publishedAt != 0);
        status.snapshotAgeSeconds = 0;

        if (status.snapshotPublished && now > snapshot.publishedAt)
        {
            status.snapshotAgeSeconds =
                static_cast<uint32>(now - snapshot.publishedAt);
        }
    }
}
