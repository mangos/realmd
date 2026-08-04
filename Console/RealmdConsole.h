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

#ifndef MANGOS_H_REALMDCONSOLE
#define MANGOS_H_REALMDCONSOLE

#include "Console/RealmdStatus.h"

#include <string>

namespace MaNGOS::Realmd
{
    /**
     * @brief Render an elapsed time as a one-token age, e.g. "3m ago".
     *
     * Coarse on purpose: the value shares a 22-column status field with the
     * realm counts, so it is one number and one unit and never two.
     */
    std::string FormatRelativeAge(uint32 seconds);

    /// Render a duration as "3d 03:51:52", matching mangosd's uptime field.
    std::string FormatUptime(uint32 seconds);

    /// Status slot 1: "4/5 (middle dot) 3m ago", or "0/0 (middle dot) never"
    /// before any snapshot has been built from the database.
    std::string FormatRealms(RealmdStatus const& status);

    /// Status slot 2: "1284 / 37 fail" -- attempts, then failures among them.
    std::string FormatConnections(RealmdStatus const& status);

    std::string FormatLogons(RealmdStatus const& status);

    /**
     * @brief The activity line: "2 patch transfers", or empty when none run.
     *
     * A free function rather than a branch inside PublishStatus so the string
     * a live transfer produces is directly testable; PublishStatus itself
     * cannot be asserted on without a terminal.
     */
    std::string FormatActivity(RealmdStatus const& status);

    /**
     * @brief Header right-hand text: listener, database health, uptime.
     *
     * Carries what does not fit in the single 22-column status row, and is the
     * only producer of that line in realmd. The database section reads
     * "DB (em dash)" until the first probe completes, which phase 3 adds.
     * Phase 4 appends the scheduled-exit countdown HERE, inside this function,
     * and never from Main.cpp.
     */
    std::string FormatHeaderRight(RealmdStatus const& status);

    /**
     * @brief Push one gathered status onto the console.
     *
     * Returns immediately when the full-screen console is not running, so the
     * caller does not have to test for it. Safe from any thread: every
     * ConsoleUI setter takes the UI's mutex and only stores state.
     *
     * The sole caller of SetStatus, SetHeaderRight and SetActivity anywhere in
     * realmd, in this and every later phase.
     */
    void PublishStatus(RealmdStatus const& status);

    /**
     * @brief Status slot 6: failed logons as a percentage, one decimal.
     *
     * An em dash when nothing has been attempted, because zero of zero is not
     * a healthy zero percent.
     */
    std::string FormatFailureRate(RealmdStatus const& status);
}

#endif
/// @}
