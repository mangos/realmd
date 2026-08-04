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

#include "Events/ScheduledExitCountdown.h"

#include "Common/TimeConstants.h"
#include "Events/EventFormat.h"

#include <cctype>

namespace MaNGOS::Realmd
{
    long ScheduledExitSecondsRemaining(
        MaNGOS::ScheduledExitSchedule const& schedule,
        std::tm const& localTime)
    {
        if (!schedule.enabled)
        {
            return -1;
        }

        long daysAhead = long(schedule.dayOfWeek) - long(localTime.tm_wday);
        if (daysAhead < 0)
        {
            daysAhead += 7;
        }

        long const nowOfDay = long(localTime.tm_hour) * HOUR +
            long(localTime.tm_min) * MINUTE + long(localTime.tm_sec);
        long const fireOfDay =
            long(schedule.hour) * HOUR + long(schedule.minute) * MINUTE;

        long remaining = daysAhead * long(DAY) + fireOfDay - nowOfDay;
        if (remaining < 0)
        {
            // This week's slot has already gone by; the next one is a week out.
            remaining += long(WEEK);
        }

        return remaining;
    }

    std::string FormatScheduledExitCountdown(
        MaNGOS::ScheduledExitSchedule const& schedule,
        std::tm const& localTime, long thresholdSeconds)
    {
        long const remaining =
            ScheduledExitSecondsRemaining(schedule, localTime);
        if (remaining < 0 || remaining > thresholdSeconds)
        {
            return std::string();
        }

        std::string text(MaNGOS::ScheduledExitModeToString(schedule.mode));
        if (!text.empty())
        {
            text[0] = char(std::toupper((unsigned char)text[0]));
        }

        text += " in ";
        text += FormatShortDuration(time_t(remaining));
        return text;
    }
}
