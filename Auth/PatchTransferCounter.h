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

#ifndef MANGOS_H_PATCHTRANSFERCOUNTER
#define MANGOS_H_PATCHTRANSFERCOUNTER

#include <chrono>
#include <cstdint>
#include <functional>

namespace MaNGOS::Realmd
{
    /**
     * @brief Number of patch transfers currently streaming.
     *
     * Counts registrations, not sockets. A transfer is live from the moment
     * StartPatchTransfer() accepts it until the detached streaming closure that
     * owns its guard has been destroyed.
     */
    std::uint32_t ActivePatchTransfers();

    /**
     * @brief One live patch transfer's registration.
     *
     * Constructed on the accepting thread and then moved, through a
     * std::unique_ptr, into the detached streaming closure. That ordering is
     * deliberate: the registration exists before the thread does, so no
     * observer can ever see an accepted transfer as absent, and it is released
     * exactly once, when the closure is destroyed -- including when
     * std::thread's constructor throws and the closure is destroyed having
     * never run.
     *
     * The closer is the transport's own tear-down callback (net::Closer, which
     * is a std::function<void()>). It is taken by value as a plain
     * std::function so this header depends on nothing but the standard library:
     * a shutdown-safety primitive must not drag net/ISession.hpp or Log.h into
     * every consumer. It defaults to an empty function so a test, or any caller
     * with nothing to cancel, can still register.
     *
     * Neither copyable nor movable: ownership travels in the unique_ptr.
     */
    class PatchTransferGuard
    {
        public:
            explicit PatchTransferGuard(
                std::function<void()> closer = std::function<void()>());
            ~PatchTransferGuard();

            PatchTransferGuard(PatchTransferGuard const&) = delete;
            PatchTransferGuard& operator=(PatchTransferGuard const&) = delete;
            PatchTransferGuard(PatchTransferGuard&&) = delete;
            PatchTransferGuard& operator=(PatchTransferGuard&&) = delete;

        private:
            std::uint64_t m_id;
    };

    /**
     * @brief Sweep the registered transfer callbacks exactly once.
     *
     * NOT a cancellation. By the time this runs, authServer.Stop() has
     * already disarmed every flow on all three transports, so each stored
     * closer is a no-op -- including for a transfer sitting between
     * chunks. Stop() performs the teardown; the drain proves it. This
     * exists so the registry is emptied deterministically and a second
     * call has nothing left to do. Best effort, and
     *        non-throwing.
     *
     * Invokes each registered closer at most once, then leaves that entry's
     * slot empty so a second call finds nothing to invoke. The entry itself
     * stays in the registry until its guard is destroyed, so the count keeps
     * telling the truth while the transfer is winding up.
     *
     * READ THIS BEFORE RELYING ON IT. A net::Closer runs the transport's
     * requestClose(), and requestClose() DOES NOT CLOSE THE SOCKET. It records
     * graceful-close intent and the connection closes only once the outbound
     * queue has drained: net/reactor/ReactorServer.cpp:264-271 with :359-370,
     * net/iocp/IocpServer.cpp:52 ("Do NOT close the socket here") with
     * :152-173, net/uring/UringServer.cpp:242-249. A peer that is not reading
     * never drains, so this call by itself does not wake a transfer parked in
     * flow->awaitWritable().
     *
     * What DOES wake such a transfer is disarm()'s out.close() -- "release any
     * bulk producer parked on backpressure" (ReactorServer.cpp:273-278,
     * IocpServer.cpp:80-84, UringServer.cpp:251-256) -- which the shutdown
     * reaches through AuthServer::Stop(). So this is cleanup layered on top of
     * a stop that has already run, useful for a transfer that is between
     * chunks rather than blocked, and never the thing that proves a producer is
     * gone. DrainPatchTransfers() proves that.
     *
     * MUST be called AFTER AuthServer::Stop(), never before. IOCP's
     * SendChannel::requestClose() snapshots the raw ConnCtx* under the channel
     * mutex and dereferences it AFTER releasing that mutex
     * (net/iocp/IocpServer.cpp:59-78), so a concurrent disarm() can null and
     * retire the context under a live cross-thread call. Calling this once
     * Stop() has returned is safe precisely because every channel is already
     * disarmed and the snapshot is null.
     *
     * Non-throwing by construction: each closer is popped out of the registry
     * on its own, invoked outside the lock and inside its own catch(...), so
     * neither a throwing callback nor a re-entrant guard release can stop the
     * remaining cancellations.
     *
     * Safe to call when nothing is registered, and safe to call twice.
     */
    void CancelPatchTransfers();

    /**
     * @brief Wait, with a hard bound, for every live patch transfer to finish.
     *
     * The streaming closures are detached, so AuthServer::Stop() does not join
     * them, yet they are console log producers: reaching static destruction
     * while one is running lets ~Log() close the log files underneath it, and
     * deletes the console writer thread's own view of the world. Shutdown must
     * equally never hang on a stalled client, hence the bound.
     *
     * Call AuthServer::Stop() FIRST. That is what makes this a short wait: the
     * transport's stop() disarms every send channel, and disarm()'s out.close()
     * is what releases a transfer parked in flow->awaitWritable()
     * (net/reactor/ReactorServer.cpp:273-278, net/iocp/IocpServer.cpp:80-84,
     * net/uring/UringServer.cpp:251-256). CancelPatchTransfers() may then be
     * called as cleanup, but it records close intent only and does not force a
     * close (net/iocp/IocpServer.cpp:52), so it is not what shortens this wait.
     *
     * The idle case is tested before any waiting, so a zero or negative timeout
     * still reports an already-quiet daemon truthfully. The wait itself is a
     * single deadline, re-tested on spurious wakeups and never extended, and it
     * wakes on the last guard's release rather than by polling.
     *
     * @param timeout Longest time to wait. Must comfortably exceed the one
     *        second the streaming closure sleeps before its first loop
     *        iteration (Auth/PatchHandler.cpp:75), or a just-accepted transfer
     *        cannot even have looked at flow control yet, let alone noticed
     *        that its channel was disarmed.
     * @return true when nothing is live; false when the timeout expired with at
     *         least one transfer still running. The caller must treat false as
     *         "producers may still be alive" and act accordingly.
     */
    bool DrainPatchTransfers(std::chrono::milliseconds timeout);
}

#endif
/// @}
