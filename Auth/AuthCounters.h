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

#ifndef MANGOS_H_REALMD_AUTHCOUNTERS
#define MANGOS_H_REALMD_AUTHCOUNTERS

#include "Platform/Define.h"

#include <atomic>

namespace MaNGOS::Realmd
{
/**
 * @brief Raise a high-water mark to @p current if it is still behind.
 *
 * Lock free, and safe to call from every network worker. compare_exchange_weak
 * reloads @p peak into the local on failure, so a racing riser that has already
 * published a higher value ends the loop instead of clobbering it.
 *
 * Note for reviewers: the unit test proves monotonicity, not atomicity. A
 * multithreaded stress test cannot tell this apart from a non-atomic
 * read-then-store, so correctness under concurrency rests on this loop and on
 * this function being the only writer of AuthSocket::s_peakConnections.
 *
 * @param peak    the high-water mark to raise
 * @param current the value just observed
 */
void RaiseHighWaterMark(std::atomic<uint32>& peak, uint32 current);
}

#endif
/// @}
