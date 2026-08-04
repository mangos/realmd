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

#ifndef MANGOS_H_ACCOUNTNAMEFOLD
#define MANGOS_H_ACCOUNTNAMEFOLD

#include <string>

namespace MaNGOS::Realmd
{
    /**
     * @brief Reduce a client-supplied account name to printable ASCII.
     *
     * Every code point outside 0x20..0x7E becomes a single '?'. The console's
     * row builder counts code points rather than display cells, so one wide
     * character would consume two terminal cells while advancing the column by
     * one and corrupt every row below it; an unauthenticated reconnect name is
     * arbitrary UTF-8 and is logged on lookup failure, which makes that path
     * remotely reachable. WoW account names are uppercase ASCII in normal
     * operation, so folding costs nothing legitimate.
     *
     * How many bytes a '?' stands for is decided only AFTER validation. A
     * multi-byte sequence is consumed whole exactly when its lead byte is a
     * legal introducer (0xC2..0xDF, 0xE0..0xEF or 0xF0..0xF4) AND every
     * continuation byte it promises is present in the string AND each of those
     * bytes matches 0b10xxxxxx. Otherwise precisely one byte is consumed.
     * Trusting the lead byte instead would let "\xC3" followed by 'A' swallow
     * the 'A' and print a name the client never sent. Either way every
     * iteration advances at least one byte, so the walk always terminates.
     *
     * What it deliberately does NOT do: it does not reject overlong three- and
     * four-byte forms, surrogate ranges, or non-characters that are otherwise
     * well formed. Each of those still folds to exactly one '?' and still
     * advances correctly, which is the whole contract; this is a display
     * safeguard, not a Unicode validator, and claiming otherwise would be the
     * kind of overstatement that stops the next reader checking.
     *
     * Deliberately dependency-free: the auth path must not include a Console/
     * header to log a name.
     *
     * @param name the raw name as it arrived from the client
     * @return a printable-ASCII rendering, safe to hand to the logger
     */
    std::string FoldAccountName(std::string const& name);
}

#endif
/// @}
