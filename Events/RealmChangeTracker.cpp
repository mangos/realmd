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

#include "Events/RealmChangeTracker.h"

#include "Common/ServerDefines.h"

namespace
{
    bool IsRealmOffline(Realm const& realm)
    {
        return (realm.realmflags & REALM_FLAG_OFFLINE) != 0;
    }

    void PushEvent(std::vector<MaNGOS::Realmd::RealmChangeEvent>& events,
                   MaNGOS::Realmd::RealmChangeEvent::Kind kind,
                   std::string const& realm)
    {
        MaNGOS::Realmd::RealmChangeEvent event;
        event.kind = kind;
        event.realm = realm;
        events.push_back(event);
    }
}

namespace MaNGOS::Realmd
{
    std::string FormatRealmChangeEvent(RealmChangeEvent const& event)
    {
        switch (event.kind)
        {
            case RealmChangeEvent::REALM_CHANGE_WENT_OFFLINE:
                return "Realm '" + event.realm + "' went offline";
            case RealmChangeEvent::REALM_CHANGE_BACK_ONLINE:
                return "Realm '" + event.realm + "' back online";
            case RealmChangeEvent::REALM_CHANGE_ADDED:
                return "New realm '" + event.realm + "' detected in realmlist";
            case RealmChangeEvent::REALM_CHANGE_REMOVED:
                return "Realm '" + event.realm + "' removed from realmlist";
        }

        return std::string();
    }

    RealmChangeTracker::RealmChangeTracker()
        : m_primed(false)
    {
    }

    bool RealmChangeTracker::Observe(RealmSnapshot const& snapshot,
                                     std::vector<RealmChangeEvent>& events)
    {
        events.clear();

        std::map<std::string, bool> current;
        for (RealmMap::const_iterator realm = snapshot.realms.begin();
             realm != snapshot.realms.end(); ++realm)
        {
            current[realm->first] = IsRealmOffline(realm->second);
        }

        // First observation: record the baseline, announce nothing.
        if (!m_primed)
        {
            m_primed = true;
            m_offlineByRealm.swap(current);
            return false;
        }

        // Both maps are ordered by realm name, so a merge walk yields the
        // events in a deterministic order and visits every realm exactly once.
        std::map<std::string, bool>::const_iterator was =
            m_offlineByRealm.begin();
        std::map<std::string, bool>::const_iterator now = current.begin();
        while (was != m_offlineByRealm.end() || now != current.end())
        {
            if (now == current.end() ||
                (was != m_offlineByRealm.end() && was->first < now->first))
            {
                PushEvent(events, RealmChangeEvent::REALM_CHANGE_REMOVED,
                          was->first);
                ++was;
            }
            else if (was == m_offlineByRealm.end() || now->first < was->first)
            {
                PushEvent(events, RealmChangeEvent::REALM_CHANGE_ADDED,
                          now->first);
                ++now;
            }
            else
            {
                if (was->second != now->second)
                {
                    PushEvent(events, now->second
                        ? RealmChangeEvent::REALM_CHANGE_WENT_OFFLINE
                        : RealmChangeEvent::REALM_CHANGE_BACK_ONLINE,
                        now->first);
                }
                ++was;
                ++now;
            }
        }

        m_offlineByRealm.swap(current);
        return !events.empty();
    }
}
