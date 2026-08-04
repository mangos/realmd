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

#ifndef MANGOS_H_SCHEDULEDEXITCOUNTDOWN
#define MANGOS_H_SCHEDULEDEXITCOUNTDOWN

#include "ScheduledExit.h"

#include <ctime>
#include <string>

namespace MaNGOS::Realmd
{
    /**
     * @brief Seconds until the next occurrence of a weekly scheduled exit.
     * @param schedule the loaded ScheduledExit configuration
     * @param localTime local wall-clock time, as produced by safe_localtime()
     * @return seconds remaining, or -1 when the schedule is disabled
     *
     * Uses the same local wall-clock fields the firing code matches on
     * (tm_wday / tm_hour / tm_min, ScheduledExit.cpp:49-56), so the countdown
     * can never disagree with the minute realmd actually restarts.
     */
    long ScheduledExitSecondsRemaining(
        MaNGOS::ScheduledExitSchedule const& schedule,
        std::tm const& localTime);

    /**
     * @brief Header fragment describing an imminent scheduled exit.
     * @param schedule the loaded ScheduledExit configuration
     * @param localTime local wall-clock time, as produced by safe_localtime()
     * @param thresholdSeconds how far ahead the countdown becomes visible
     * @return "Restart in 12m30s", or an empty string when the schedule is
     *         disabled or the next exit is further away than
     *         @p thresholdSeconds
     */
    std::string FormatScheduledExitCountdown(
        MaNGOS::ScheduledExitSchedule const& schedule,
        std::tm const& localTime, long thresholdSeconds = 3600);
}

#endif
/// @}
