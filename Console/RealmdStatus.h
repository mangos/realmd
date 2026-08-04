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

#ifndef MANGOS_H_REALMDSTATUS
#define MANGOS_H_REALMDSTATUS

#include "Platform/Define.h"
#include "Realm/RealmSnapshot.h"

#include <ctime>
#include <string>

namespace MaNGOS::Realmd
{
    /**
     * @brief One tick's worth of observable realmd state.
     *
     * A plain aggregate with no behaviour: StatusSource fills it, the console
     * formatters read it, and the unit tests build one by hand. Every member
     * carries a default member initialiser, so a default-constructed
     * RealmdStatus renders correctly rather than printing stack garbage.
     *
     * These flat members are the whole plan's authoritative shape, and this is
     * the ONLY place any of them is declared:
     *
     *  - Phase 3 adds no members. It translates its LogonCounts and
     *    LoginDbHealth structs onto the fields below inside
     *    StatusSource::Gather.
     *  - Phase 4 adds no members either. churn and scheduledExit are declared
     *    HERE, in phase 2, so that the struct, the formatters and the formatter
     *    tests are written exactly once. A phase 4 task that re-declares either
     *    one produces a duplicate member, not a red test.
     */
    struct RealmdStatus
    {
        /// Configured BindIP. Normally "0.0.0.0"; empty means the same thing.
        std::string bindIp;
        /// Configured RealmServerPort.
        uint16 port = 0;
        /// Patch serving enabled by configuration (Patch.Enable).
        bool patchEnabled = false;

        /// Open auth TCP connections.
        uint32 connections = 0;
        /// Authenticated clients sitting at realm select.
        uint32 authWaiting = 0;
        /// High-water mark of connections. Populated in phase 3.
        uint32 peakConnections = 0;

        /// Realms in the published snapshot.
        uint32 realmsTotal = 0;
        /// Of those, the ones not flagged REALM_FLAG_OFFLINE.
        uint32 realmsOnline = 0;
        /// Seconds since that snapshot was published.
        uint32 snapshotAgeSeconds = 0;
        /// False before any snapshot has been built from the database.
        bool snapshotPublished = false;

        /// Completed logons, fresh and reconnect. Populated in phase 3.
        uint32 logonsOk = 0;
        /// Challenge rejections: unknown, locked or banned. Phase 3.
        uint32 logonsFailedRejected = 0;
        /// Proof failures, fresh and reconnect. Phase 3.
        uint32 logonsFailedBadProof = 0;
        /// SendInvalidVersion: bad build or missing patch. Phase 3.
        uint32 logonsFailedBuild = 0;

        /// Patch transfers currently streaming, from ActivePatchTransfers().
        /// Populated in phase 2: it drives the console's activity line, and a
        /// field nothing ever sets would leave that line permanently blank.
        uint32 patchTransfersActive = 0;

        /// False until the first login-database probe completes. Phase 3.
        bool dbProbed = false;
        /// Result of the most recent probe. Meaningless while !dbProbed.
        bool dbOk = false;
        /// Duration of the most recent successful probe, including the wait for
        /// a pooled connection. A probe time, not a database round trip.
        /// Phase 3.
        uint32 dbLatencyMs = 0;

        /// Seconds since realmd began serving.
        uint32 uptimeSeconds = 0;

        /// Pre-formatted connection churn for status slot 7. Filled by phase 4;
        /// DECLARED HERE. Phase 4 must not re-declare it.
        std::string churn;
        /// Pre-formatted scheduled-exit countdown, appended to the header line
        /// when non-empty. Filled by phase 4; DECLARED HERE. Phase 4 must not
        /// re-declare it.
        std::string scheduledExit;
    };

    /**
     * @brief Fill the realm members of @p status from a published snapshot.
     *
     * Reads only; it never refreshes. A snapshot whose publishedAt is zero was
     * never built from the database, and reports as unpublished with no age. A
     * clock that has moved backwards yields an age of zero rather than a
     * wrapped unsigned value.
     *
     * @param snapshot the generation to summarise
     * @param now      current wall-clock time, in the same epoch as publishedAt
     * @param status   the aggregate whose realm members are overwritten
     */
    void ApplyRealmSnapshot(
        RealmSnapshot const& snapshot, time_t now, RealmdStatus& status);
}

#endif
/// @}
