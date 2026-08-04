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

#include "Console/StatusSource.h"

#include "Auth/AuthSocket.h"
#include "Auth/PatchTransferCounter.h"
#include "Realm/RealmList.h"

#include <utility>

namespace MaNGOS::Realmd
{
    StatusSource::StatusSource(
        std::string bindIp, uint16 port, bool patchEnabled, time_t startedAt)
        : m_bindIp(std::move(bindIp)),
          m_port(port),
          m_patchEnabled(patchEnabled),
          m_startedAt(startedAt)
    {
    }

    RealmdStatus StatusSource::Gather(time_t now)
    {
        RealmdStatus status;

        status.bindIp = m_bindIp;
        status.port = m_port;
        status.patchEnabled = m_patchEnabled;

        status.connections = AuthSocket::GetConnectionCount();
        status.authWaiting = AuthSocket::GetAuthWaitingCount();

        // Drives the console activity line. Counts guards, not sockets, so a
        // transfer is live from the moment it is accepted until its detached
        // closure has been destroyed.
        status.patchTransfersActive =
            static_cast<uint32>(MaNGOS::Realmd::ActivePatchTransfers());

        if (now > m_startedAt)
        {
            status.uptimeSeconds = static_cast<uint32>(now - m_startedAt);
        }

        // Read the published generation. Never sRealmList.UpdateIfNeed() here:
        // this runs on the housekeeping thread, which would win RunIfDue()'s
        // try_to_lock essentially every time and take the whole refresh --
        // database query plus DNS -- onto the loop that drives auth-session
        // expiry, the MySQL ping and the scheduled exit.
        RealmSnapshotStore::SnapshotPtr snapshot = sRealmList.GetSnapshot();
        if (snapshot)
        {
            ApplyRealmSnapshot(*snapshot, now, status);
        }

        return status;
    }
}
