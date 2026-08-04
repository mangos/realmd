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

/**
 * @brief Terminal outcome of one connection's authentication attempt.
 */
enum class LogonOutcome : uint8
{
    Ok,         ///< SRP6 proof accepted, logon or reconnect
    Rejected,   ///< unknown, IP-locked, banned or suspended account; or no stored session key
    BadProof,   ///< the client's proof did not match
    BuildPatch  ///< unsupported client build, or the required patch archive is missing
};

/**
 * @brief Snapshot of the process-wide logon counters.
 *
 * Total() is deliberately not the connection count: a connection that is
 * offered a patch, or that is dropped before it reaches a decision, reaches no
 * terminal outcome and is counted in no category.
 */
struct LogonCounts
{
    uint32 ok = 0;
    uint32 rejected = 0;
    uint32 badProof = 0;
    uint32 buildPatch = 0;

    /// Attempts that ended in any failure category.
    uint32 Failures() const { return rejected + badProof + buildPatch; }

    /// Attempts that reached a terminal outcome of any kind.
    uint32 Total() const { return ok + Failures(); }
};

/**
 * @brief Add one attempt to the process-wide counters.
 *
 * The auth path never calls this directly; it goes through
 * LogonAttemptRecorder so one connection cannot contribute twice.
 * Tests/CheckLogonCounters.cmake fails the build if AuthSocket.cpp ever names
 * it.
 */
void CountLogonOutcome(LogonOutcome outcome);

/// Snapshot of the counters, read once per tick by StatusSource::Gather.
LogonCounts GetLogonCounts();

/// Zero every counter. Test support only; the daemon never calls it.
void ResetLogonCounts();

/**
 * @brief Per-connection latch over CountLogonOutcome.
 *
 * A connection reaches at most one terminal outcome, but which handler decides
 * that depends on how far the client got. Owning the latch on the socket makes
 * "at most one increment per connection" structural rather than an argument
 * about control flow, and keeps it true if the handlers are rearranged.
 *
 * Not thread safe, and does not need to be: every call site runs inside
 * net::ISession::onData(), which the transport serialises per connection --
 * the same guarantee that lets _status and m_readBuf be plain members.
 */
class LogonAttemptRecorder
{
    public:
        /// Count @p outcome unless this connection has already counted one.
        void Record(LogonOutcome outcome);

        /// True once an outcome has been counted for this connection.
        bool Recorded() const { return m_recorded; }

    private:
        bool m_recorded = false;
};
}

#endif
/// @}
