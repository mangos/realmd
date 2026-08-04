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

#ifndef MANGOS_H_REALMCHANGETRACKER
#define MANGOS_H_REALMCHANGETRACKER

#include "Platform/Define.h"
#include "Realm/RealmSnapshot.h"

#include <map>
#include <string>
#include <vector>

namespace MaNGOS::Realmd
{
    /**
     * @brief One realm-list transition, ready to be turned into a log line.
     */
    struct RealmChangeEvent
    {
        enum Kind
        {
            REALM_CHANGE_WENT_OFFLINE = 0,
            REALM_CHANGE_BACK_ONLINE,
            REALM_CHANGE_ADDED,
            REALM_CHANGE_REMOVED
        };

        Kind kind;
        std::string realm;
    };

    /**
     * @brief Render one realm transition as the operator-visible log line.
     * @param event the transition to describe
     * @return the complete line, without a trailing newline
     */
    std::string FormatRealmChangeEvent(RealmChangeEvent const& event);

    /**
     * @brief Diff successive realm snapshots and report only the transitions.
     *
     * Holds the previous state as realm name to "is offline", so it never pins a
     * retired snapshot alive and its footprint does not grow with snapshot
     * churn.
     */
    class RealmChangeTracker
    {
        public:
            RealmChangeTracker();

            /**
             * @brief Compare @p snapshot against the previously observed one.
             * @param snapshot the currently published realm snapshot
             * @param events cleared, then filled with one entry per transition,
             *        in realm-name order. Left empty when nothing changed.
             * @return true when at least one transition was reported.
             *
             * The first call records the baseline and reports nothing, so a
             * freshly started realmd does not announce every realm it was
             * configured with.
             */
            bool Observe(RealmSnapshot const& snapshot,
                         std::vector<RealmChangeEvent>& events);

            /// True once the first Observe() has recorded a baseline.
            bool Primed() const { return m_primed; }

        private:
            bool m_primed;
            std::map<std::string, bool> m_offlineByRealm;
    };
}

#endif
/// @}
