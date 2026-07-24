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

#include "PatchHandler.h"
#include "PatchArtifact.h"
#include "AuthCodes.h"
#include "Log.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    /// Bytes carried per CMD_XFER_DATA packet. 4 KiB matches the historical page
    /// size the classic downloader used.
    constexpr size_t kChunkSize = 4096;

    /// Backpressure ceiling: the streaming thread will not get further than this many
    /// bytes ahead of what the transport has actually written to the socket, so queued
    /// memory stays bounded regardless of archive size or client speed. Stated in
    /// bytes because the transport coalesces queued packets into one contiguous
    /// stream, which makes a count of outstanding buffers meaningless.
    constexpr uint64_t kMaxOutstandingBytes = 256 * 1024;
}

bool StartPatchTransfer(net::Sender sender, net::Closer closer,
                        std::shared_ptr<net::FlowControl> flow,
                        std::unique_ptr<PatchArtifact> artifact,
                        std::uint64_t startOffset)
{
    if (!artifact || startOffset > artifact->size())
    {
        return false;
    }
    if (!artifact->Seek(startOffset))
    {
        return false;
    }

    std::thread([artifact = std::move(artifact),
                 sender = std::move(sender), closer = std::move(closer),
                 flow = std::move(flow)]() mutable
    {
        // The classic downloader dislikes data arriving before it has settled on
        // the transfer; a short initial pause mirrors the old PatchHandler.
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // One framing buffer for the whole transfer: the header is rewritten in place
        // each pass and the payload read straight in behind it, so streaming a 100 MB
        // archive allocates exactly once rather than once per 4 KiB chunk.
        std::vector<uint8_t> pkt(3 + kChunkSize);

        for (;;)
        {
            // Block until the transport has drained the outbound backlog below the
            // ceiling (real backpressure), or bail out if the connection is gone.
            if (flow && !flow->awaitWritable(kMaxOutstandingBytes))
            {
                return;
            }

            std::streamsize const got =
                artifact->Read(pkt.data() + 3, kChunkSize);
            if (got <= 0)
            {
                break;
            }

            // Frame: CMD_XFER_DATA, uint16 size (little-endian), payload.
            pkt[0] = uint8_t(CMD_XFER_DATA);
            pkt[1] = uint8_t(got & 0xFF);
            pkt[2] = uint8_t((got >> 8) & 0xFF);
            sender(pkt.data(), 3 + static_cast<size_t>(got));
        }

        if (artifact->bad())
        {
            sLog.outError("PatchTransfer: read error streaming patch, closing connection");
            closer();
        }
        // On success the socket is left open; the client tears it down once it has
        // the whole archive and reconnects with the patched build.
    }).detach();

    return true;
}
