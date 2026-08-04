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

/** \file
 \ingroup realmd
 */

#include "PatchTransferCounter.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <utility>

namespace MaNGOS::Realmd
{

namespace
{
    struct PatchTransferState
    {
        std::mutex                                     mutex;
        std::condition_variable                        idle;
        std::map<std::uint64_t, std::function<void()>> closers;
        std::uint64_t                                  nextId;

        PatchTransferState() : nextId(0) {}
    };

    /**
     * Deliberately never destroyed. The shutdown drain is bounded, so a
     * transfer that outlives the bound also outlives main(). Had this state
     * static storage duration, its destructor would run while that thread still
     * held a guard, and releasing the guard would lock a destroyed mutex.
     * Leaking one small object at process exit is the cheap, correct answer.
     */
    PatchTransferState& State()
    {
        static PatchTransferState* const state = new PatchTransferState();
        return *state;
    }
}

std::uint32_t ActivePatchTransfers()
{
    std::lock_guard<std::mutex> lock(State().mutex);
    return std::uint32_t(State().closers.size());
}

PatchTransferGuard::PatchTransferGuard(std::function<void()> closer)
    : m_id(0)
{
    std::lock_guard<std::mutex> lock(State().mutex);

    // Monotonic and never reused, so a guard can only ever erase its own entry
    // even if a later transfer starts while this one is still being torn down.
    m_id = ++State().nextId;
    State().closers.emplace(m_id, std::move(closer));
}

PatchTransferGuard::~PatchTransferGuard()
{
    bool nowIdle = false;
    {
        std::lock_guard<std::mutex> lock(State().mutex);
        State().closers.erase(m_id);
        nowIdle = State().closers.empty();
    }

    // Notified outside the lock so a waiting shutdown wakes to an unlocked
    // mutex rather than immediately blocking on the one we still hold. Task 4's
    // drain depends on this notification existing.
    if (nowIdle)
    {
        State().idle.notify_all();
    }
}

void CancelPatchTransfers()
{
    // One closer at a time: take the lock, move exactly one callable out of the
    // registry, drop the lock, invoke it inside its own catch(...), repeat.
    // Three separate reasons, and none of them is style.
    //
    // 1. This must not throw. It is called from realmd's shutdown, where an
    //    escaping exception would abort every cancellation still queued behind
    //    it. A closer is transport code and may throw; std::function's
    //    operator() can itself throw std::bad_function_call. So each invocation
    //    gets its own catch(...) and the loop carries on.
    // 2. It must not run under the lock. The closer reaches the transport,
    //    which may complete a transfer synchronously; that transfer's guard
    //    destructor takes THIS mutex, and std::mutex is not recursive, so
    //    invoking under the lock risks a re-entrant deadlock -- and would in
    //    any case serialise the whole cancellation behind every tear-down.
    // 3. It must not copy the whole map. A snapshot invoked after the lock is
    //    released can call a closer belonging to a transfer that finished in
    //    the meantime, and a throw part way through a snapshot loses the rest.
    //
    // The callable is MOVED OUT and its slot left empty rather than the entry
    // erased: the entry belongs to the live guard, and erasing it here would
    // drop ActivePatchTransfers() to zero while the transfer is still running.
    // Emptying the slot is also what makes a second call a no-op.
    //
    // The cursor is an id, not an iterator. Ids are monotonic and never reused,
    // so upper_bound() resumes correctly even though the entry it names may
    // have been erased by its own guard while the lock was released.
    std::uint64_t cursor = 0;

    for (;;)
    {
        std::function<void()> closer;
        {
            std::lock_guard<std::mutex> lock(State().mutex);

            auto entry = State().closers.upper_bound(cursor);
            while (entry != State().closers.end() && !entry->second)
            {
                ++entry;
            }
            if (entry == State().closers.end())
            {
                return;
            }

            cursor = entry->first;
            closer = std::move(entry->second);
            entry->second = std::function<void()>();
        }

        try
        {
            closer();
        }
        catch (...)
        {
            // Swallowed on purpose: one transport callback misbehaving must not
            // stop the remaining callbacks being swept, and must not
            // unwind into realmd's shutdown.
        }
    }
}

bool DrainPatchTransfers(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(State().mutex);

    // Tested before any waiting: the ordinary shutdown has no transfer in
    // flight and must return instantly, and a caller passing no timeout at all
    // still deserves a truthful answer rather than a reflexive failure.
    if (State().closers.empty())
    {
        return true;
    }
    if (timeout <= std::chrono::milliseconds::zero())
    {
        return false;
    }

    // One deadline, re-tested on spurious wakeups and never extended by them.
    return State().idle.wait_for(lock, timeout,
        [] { return State().closers.empty(); });
}

}
