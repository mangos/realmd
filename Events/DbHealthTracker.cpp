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

/** \file \ingroup realmd */

#include "Events/DbHealthTracker.h"

#include "Events/EventFormat.h"

#include <cstdio>

namespace
{
    /// Elapsed seconds from @p since to @p now, never negative.
    ///
    /// The wall clock can step backwards. A negative elapsed value must not
    /// leak into the reported text, and -- more importantly -- its sign must
    /// never be used to decide whether an event ever happened. "Has a probe
    /// succeeded?" is `lastSuccess != 0`, tested separately by the caller.
    time_t ClampElapsed(time_t now, time_t since)
    {
        if (since == 0 || now <= since)
        {
            return 0;
        }

        return now - since;
    }
}

namespace MaNGOS::Realmd
{
    DbHealthTracker::DbHealthTracker(time_t staleAfter)
        : m_staleAfter(staleAfter),
          m_state(DB_HEALTH_UNKNOWN),
          m_stateSince(0)
    {
    }

    void DbHealthTracker::SetStaleAfter(time_t seconds)
    {
        m_staleAfter = seconds > 0 ? seconds : 120;
    }

    bool DbHealthTracker::Observe(LoginDbHealth const& health, time_t now,
                                  std::string& event)
    {
        event.clear();

        bool const hasSuccess = (health.lastSuccess != 0);
        time_t const successAge = ClampElapsed(now, health.lastSuccess);

        // Raw probe state -> derived health. Ok plus an old last success is
        // STALE: the reading is good but the cadence that produces readings has
        // stopped, which is a different failure from the database being down.
        DbHealth next = DB_HEALTH_UNKNOWN;
        switch (health.state)
        {
        case DbProbeState::Unknown:
            next = DB_HEALTH_UNKNOWN;
            break;
        case DbProbeState::Down:
            next = DB_HEALTH_DOWN;
            break;
        case DbProbeState::Ok:
            next = (hasSuccess && successAge > m_staleAfter)
                ? DB_HEALTH_STALE : DB_HEALTH_OK;
            break;
        }

        if (next == m_state)
        {
            return false;
        }

        DbHealth const previous = m_state;
        time_t const heldFor = ClampElapsed(now, m_stateSince);

        m_state = next;
        m_stateSince = now;

        char text[192];
        switch (next)
        {
        case DB_HEALTH_DOWN:
            if (!hasSuccess)
            {
                snprintf(text, sizeof(text),
                    "Login database down (probe failed; "
                    "no successful probe yet)");
            }
            else
            {
                snprintf(text, sizeof(text),
                    "Login database down (probe failed; last success %s ago)",
                    FormatShortDuration(successAge).c_str());
            }
            break;
        case DB_HEALTH_STALE:
            snprintf(text, sizeof(text),
                "Login database probe stale (last success %s ago)",
                FormatShortDuration(successAge).c_str());
            break;
        case DB_HEALTH_OK:
            if (previous == DB_HEALTH_DOWN)
            {
                snprintf(text, sizeof(text),
                    "Login database recovered (probe %u ms; down for %s)",
                    health.latencyMs, FormatShortDuration(heldFor).c_str());
            }
            else if (previous == DB_HEALTH_STALE)
            {
                snprintf(text, sizeof(text),
                    "Login database probe fresh again "
                    "(probe %u ms; stale for %s)",
                    health.latencyMs, FormatShortDuration(heldFor).c_str());
            }
            else
            {
                snprintf(text, sizeof(text),
                    "Login database probe ok (%u ms)", health.latencyMs);
            }
            break;
        default:
            // Transition back to UNKNOWN cannot happen: the process-wide record
            // never returns to Unknown once probed. State is still updated
            // above, but there is nothing worth logging.
            return false;
        }

        event.assign(text);
        return true;
    }
}
