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

#include "Events/EventFormat.h"

#include "Common/TimeConstants.h"

#include <cstdio>

namespace MaNGOS::Realmd
{
    std::string FormatShortDuration(time_t seconds)
    {
        if (seconds < 0)
        {
            seconds = 0;
        }

        char text[32];
        if (seconds < MINUTE)
        {
            snprintf(text, sizeof(text), "%ds", int(seconds));
        }
        else if (seconds < HOUR)
        {
            snprintf(text, sizeof(text), "%dm%02ds",
                     int(seconds / MINUTE), int(seconds % MINUTE));
        }
        else if (seconds < DAY)
        {
            snprintf(text, sizeof(text), "%dh%02dm",
                     int(seconds / HOUR), int((seconds % HOUR) / MINUTE));
        }
        else
        {
            snprintf(text, sizeof(text), "%dd%02dh",
                     int(seconds / DAY), int((seconds % DAY) / HOUR));
        }

        return std::string(text);
    }
}
