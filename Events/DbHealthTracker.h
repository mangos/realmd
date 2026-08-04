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

#ifndef MANGOS_H_DBHEALTHTRACKER
#define MANGOS_H_DBHEALTHTRACKER

#include "Console/LoginDbHealth.h"
#include "Platform/Define.h"

#include <ctime>
#include <string>

namespace MaNGOS::Realmd
{
    /**
     * @brief Derived health of the login database, including staleness.
     *
     * Distinct from DbProbeState in Console/LoginDbHealth.h, which is the raw
     * result of the most recent probe. This adds the fourth case the raw state
     * cannot express: the last probe succeeded, but that was long enough ago
     * that the probe cadence itself has evidently stopped running.
     */
    enum DbHealth
    {
        DB_HEALTH_UNKNOWN = 0,  ///< no probe has completed yet
        DB_HEALTH_OK,           ///< last probe succeeded, recently
        DB_HEALTH_STALE,        ///< probes stopped running; last success is old
        DB_HEALTH_DOWN          ///< last probe failed
    };

    /**
     * @brief Turn a stream of probe readings into down / recovered / stale
     *        events.
     *
     * Reports only on a change of derived state, so a healthy realmd stays
     * silent no matter how often the probe runs.
     */
    class DbHealthTracker
    {
        public:
            /**
             * @param staleAfter seconds without a successful probe after which
             *        an otherwise-healthy database is reported as stale
             */
            explicit DbHealthTracker(time_t staleAfter = 120);

            /// Adjust the staleness threshold once the probe cadence is known.
            void SetStaleAfter(time_t seconds);

            /**
             * @brief Fold one probe reading in.
             * @param health the process-wide record, as returned by
             *        GetLoginDbHealth()
             * @param now current wall-clock time, on the same clock as
             *        health.lastSuccess. A backwards step clamps every reported
             *        duration to zero; it never changes which branch is taken.
             * @param event cleared, then filled with the line to log when the
             *        derived state changed. Left empty otherwise.
             * @return true when @p event was filled.
             */
            bool Observe(LoginDbHealth const& health, time_t now,
                         std::string& event);

            DbHealth State() const { return m_state; }

        private:
            time_t   m_staleAfter;
            DbHealth m_state;
            time_t   m_stateSince;
    };
}

#endif
/// @}
