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

#include "AuthCounters.h"


namespace
{
// Namespace scope and constant-initialised, so the counters outlive every
// socket and every network worker: an increment arriving late in shutdown can
// never touch storage that has already been destroyed. Being trivially
// destructible is also what makes them safe on the lifecycle invariant's
// std::_Exit path, which skips static destruction entirely. That is why they
// are plain atomics rather than the deliberately leaked heap state phase 1's
// PatchTransferState uses -- that one owns a mutex and a condition_variable,
// and those do have destructors.
std::atomic<uint32> s_logonOk{0};
std::atomic<uint32> s_logonRejected{0};
std::atomic<uint32> s_logonBadProof{0};
std::atomic<uint32> s_logonBuildPatch{0};
}

namespace MaNGOS::Realmd
{
void RaiseHighWaterMark(std::atomic<uint32>& peak, uint32 current)
{
    uint32 observed = peak.load(std::memory_order_relaxed);
    while (observed < current &&
           !peak.compare_exchange_weak(
               observed, current,
               std::memory_order_relaxed, std::memory_order_relaxed))
    {
    }
}

void CountLogonOutcome(LogonOutcome outcome)
{
    switch (outcome)
    {
        case LogonOutcome::Ok:
            s_logonOk.fetch_add(1, std::memory_order_relaxed);
            break;
        case LogonOutcome::Rejected:
            s_logonRejected.fetch_add(1, std::memory_order_relaxed);
            break;
        case LogonOutcome::BadProof:
            s_logonBadProof.fetch_add(1, std::memory_order_relaxed);
            break;
        case LogonOutcome::BuildPatch:
            s_logonBuildPatch.fetch_add(1, std::memory_order_relaxed);
            break;
    }
}

LogonCounts GetLogonCounts()
{
    LogonCounts counts;
    counts.ok = s_logonOk.load(std::memory_order_relaxed);
    counts.rejected = s_logonRejected.load(std::memory_order_relaxed);
    counts.badProof = s_logonBadProof.load(std::memory_order_relaxed);
    counts.buildPatch = s_logonBuildPatch.load(std::memory_order_relaxed);
    return counts;
}

void ResetLogonCounts()
{
    s_logonOk.store(0, std::memory_order_relaxed);
    s_logonRejected.store(0, std::memory_order_relaxed);
    s_logonBadProof.store(0, std::memory_order_relaxed);
    s_logonBuildPatch.store(0, std::memory_order_relaxed);
}

void LogonAttemptRecorder::Record(LogonOutcome outcome)
{
    if (m_recorded)
    {
        return;
    }

    m_recorded = true;
    CountLogonOutcome(outcome);
}
}
