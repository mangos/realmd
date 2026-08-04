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

#ifndef MANGOS_H_STATUSSOURCE
#define MANGOS_H_STATUSSOURCE

#include "Console/RealmdStatus.h"

#include <ctime>
#include <string>
#include "ScheduledExit.h"

namespace MaNGOS::Realmd
{
    /**
     * @brief Collects one RealmdStatus from the running daemon.
     *
     * Holds the values that are fixed for the process lifetime (listener,
     * patch setting, start time) and reads the volatile ones on each call.
     * Every read is non-blocking: the realm data comes from the published
     * snapshot, never from a refresh, because a refresh runs a database query
     * and up to three untimed getaddrinfo() calls per realm and would stall
     * the housekeeping loop that drives auth expiry and the scheduled exit.
     */
    class StatusSource
    {
        public:
            /**
             * @param bindIp       configured BindIP, normally "0.0.0.0"
             * @param port         configured RealmServerPort
             * @param patchEnabled PatchPolicy::Enabled(), read before the move
             * @param startedAt    when realmd began serving
             */
            StatusSource(
                std::string bindIp, uint16 port, bool patchEnabled,
                time_t startedAt);

            /**
             * @brief Sample the daemon.
             *
             * NON-CONST, permanently and by design. Phase 4 gives this class a
             * ChurnWindow member that Gather advances once per tick, so it
             * mutates. Declaring it non-const now means phase 4 edits only the
             * body; a later phase must NEVER re-declare this signature, and
             * must never "tidy" it to const.
             *
             * @param now current wall-clock time
             * @return a fully populated status for this tick
             */
            RealmdStatus Gather(time_t now);

            /**
             * @brief Publish the loaded weekly exit schedule.
             *
             * Main.cpp calls this once, immediately after construction:
             * LoadScheduledExitConfig() has already run by then (Main.cpp:425)
             * and realmd never reloads its configuration, so one copy is
             * enough. Taking a copy keeps the schedule out of a second global
             * and keeps the countdown itself a pure function.
             */
            void SetScheduledExit(
                MaNGOS::ScheduledExitSchedule const& schedule);

        private:
            std::string m_bindIp;
            uint16 m_port;
            bool m_patchEnabled;
            time_t m_startedAt;
            MaNGOS::ScheduledExitSchedule m_scheduledExit;
            // Phase 4 adds `ChurnWindow m_churn;` here, and nothing else.
    };
}

#endif
/// @}
