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

#ifndef MANGOS_H_REALMD_LOGINDBHEALTH
#define MANGOS_H_REALMD_LOGINDBHEALTH

#include "Platform/Define.h"

#include <ctime>

namespace MaNGOS::Realmd
{
/**
 * @brief Outcome of the most recent login-database probe.
 */
enum class DbProbeState : uint8
{
    Unknown,    ///< no probe has completed yet -- explicitly not "ok"
    Ok,         ///< the last probe returned a result set
    Down        ///< the last probe returned nothing
};

/**
 * @brief What realmd's own login-database probe has observed so far.
 *
 * Raw observation only. The DERIVED state, which adds staleness, is phase 4's
 * DbHealth; DbHealthTracker::Observe takes this struct directly.
 */
struct LoginDbHealth
{
    DbProbeState state = DbProbeState::Unknown;

    /// Elapsed time of the last probe call, successful or not. Not a round
    /// trip: Database::Query() takes a pooled-connection lock first, so this
    /// includes any wait for a free connection.
    uint32 latencyMs = 0;

    /// 0 until one probe has succeeded.
    time_t lastSuccess = 0;
};

/**
 * @brief Fold one completed probe into @p health.
 *
 * A failed probe keeps the previous lastSuccess, so phase 4 can say how long
 * the database has been unreachable rather than only that it is.
 *
 * @param health    record to update
 * @param ok        whether the probe query returned a result
 * @param latencyMs measured duration of the probe call
 * @param now       wall-clock time the probe completed
 */
void RecordLoginDbProbe(LoginDbHealth& health, bool ok, uint32 latencyMs, time_t now);

/**
 * @brief Fold one completed probe into the process-wide record.
 *
 * Main thread only. realmd probes and renders from the same housekeeping loop,
 * so the record needs no synchronisation and deliberately has none.
 */
void RecordLoginDbProbe(bool ok, uint32 latencyMs, time_t now);

/// Copy of the process-wide record. Main thread only.
LoginDbHealth GetLoginDbHealth();

/// Return the process-wide record to unprobed. Test support only.
void ResetLoginDbHealth();
}

#endif
/// @}
