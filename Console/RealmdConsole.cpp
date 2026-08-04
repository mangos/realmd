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

#include "Console/RealmdConsole.h"

#include "Common/TimeConstants.h"
#include "Console/ConsoleUI.h"

#include <cstdio>

namespace MaNGOS::Realmd
{
    std::string FormatRelativeAge(uint32 seconds)
    {
        char buffer[32];

        if (seconds < MINUTE)
        {
            snprintf(buffer, sizeof(buffer), "%us ago", seconds);
        }
        else if (seconds < HOUR)
        {
            snprintf(buffer, sizeof(buffer), "%um ago", seconds / MINUTE);
        }
        else if (seconds < DAY)
        {
            snprintf(buffer, sizeof(buffer), "%uh ago", seconds / HOUR);
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "%ud ago", seconds / DAY);
        }

        return std::string(buffer);
    }

    std::string FormatUptime(uint32 seconds)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ud %02u:%02u:%02u",
            seconds / DAY, (seconds / HOUR) % 24, (seconds / MINUTE) % 60,
            seconds % 60);
        return std::string(buffer);
    }

    std::string FormatRealms(RealmdStatus const& status)
    {
        std::string text = std::to_string(status.realmsOnline);
        text += '/';
        text += std::to_string(status.realmsTotal);
        text += " \xC2\xB7 ";

        if (status.snapshotPublished)
        {
            text += FormatRelativeAge(status.snapshotAgeSeconds);
        }
        else
        {
            text += "never";
        }

        return text;
    }

    std::string FormatLogons(RealmdStatus const& status)
    {
        uint32 const failed = status.logonsFailedRejected +
            status.logonsFailedBadProof + status.logonsFailedBuild;

        std::string text = std::to_string(status.logonsOk + failed);
        text += " / ";
        text += std::to_string(failed);
        text += " fail";
        return text;
    }

    std::string FormatActivity(RealmdStatus const& status)
    {
        if (status.patchTransfersActive == 0)
        {
            return std::string();
        }

        std::string text = std::to_string(status.patchTransfersActive);
        text += (status.patchTransfersActive == 1)
            ? " patch transfer" : " patch transfers";
        return text;
    }

    std::string FormatHeaderRight(RealmdStatus const& status)
    {
        // BindIP is normally "0.0.0.0" from the config default
        // (realmd.conf.dist.in:159, Main.cpp:480); a hand-cleared value means
        // the same thing, so render it identically.
        std::string text = status.bindIp.empty()
            ? std::string("0.0.0.0") : status.bindIp;
        text += ':';
        text += std::to_string(status.port);

        text += " \xC2\xB7 DB ";
        if (!status.dbProbed)
        {
            text += "\xE2\x80\x94";
        }
        else if (!status.dbOk)
        {
            text += "down";
        }
        else
        {
            char latency[16];
            snprintf(latency, sizeof(latency), "ok %ums", status.dbLatencyMs);
            text += latency;
        }

        text += " \xC2\xB7 up ";
        text += FormatUptime(status.uptimeSeconds);

        // The scheduled-exit countdown is appended here and nowhere else:
        // FormatHeaderRight is the sole owner of the header line, and
        // PublishStatus is the sole caller of SetHeaderRight. U+00B7 is written
        // as explicit UTF-8 bytes so this file stays ASCII on disk.
        if (!status.scheduledExit.empty())
        {
            text += " \xC2\xB7 ";
            text += status.scheduledExit;
        }

        return text;
    }

    void PublishStatus(RealmdStatus const& status)
    {
        MaNGOS::Console::ConsoleUI& ui = MaNGOS::Console::ConsoleUI::Instance();
        if (!ui.Active())
        {
            return;
        }

        // Slot order is load-bearing. ComposeStatus pads field i to column
        // 2 + i * 22 and RowBuilder clips at the terminal edge, so at 80
        // columns only slots 0-3 are on screen -- and slot 3 only because a
        // realistic Logons value is under 22 cells wide; wider terminals
        // reveal the rest. Nothing outside this function writes a status slot.
        ui.SetStatus(0, "Conn", std::to_string(status.connections));
        ui.SetStatus(1, "Realms", FormatRealms(status),
            status.realmsOnline < status.realmsTotal
                ? MaNGOS::Console::STYLE_WARN
                : MaNGOS::Console::STYLE_NORMAL);
        // Slot 2. A quarter of attempts failing is a flood, a misconfigured
        // client build or a broken account table, none of which should read as
        // normal. Integer arithmetic, no division and no second failure-rate
        // helper: FormatFailureRate owns the displayed percentage in slot 6,
        // this only decides a colour. The uint64 intermediates keep the
        // comparison exact past 43 million attempts, where uint32 * 100 wraps.
        uint32 const failedAttempts =
            status.logonsFailedRejected + status.logonsFailedBadProof +
            status.logonsFailedBuild;
        uint32 const totalAttempts = status.logonsOk + failedAttempts;
        bool const failureFlood =
            totalAttempts > 0 &&
            static_cast<uint64>(failedAttempts) * 100 >=
                static_cast<uint64>(totalAttempts) * 25;

        ui.SetStatus(2, "Logons", FormatLogons(status),
            failureFlood ? MaNGOS::Console::STYLE_WARN
                         : MaNGOS::Console::STYLE_NORMAL);
        ui.SetStatus(3, "Sel", std::to_string(status.authWaiting));
        ui.SetStatus(4, "Peak", std::to_string(status.peakConnections));
        ui.SetStatus(5, "Patch", status.patchEnabled ? "on" : "off");

        ui.SetStatus(6, "Fail", FormatFailureRate(status));

        ui.SetHeaderRight(FormatHeaderRight(status));

        // Cleared by writing an empty activity, never by SetProgress(-1):
        // that clears m_activity as a side effect (ConsoleUI.cpp:389-406), so
        // calling it after SetActivity would erase the line just written. The
        // progress bar stays unused.
        ui.SetActivity(FormatActivity(status));
    }

    std::string FormatFailureRate(RealmdStatus const& status)
    {
        uint32 const failed = status.logonsFailedRejected +
            status.logonsFailedBadProof + status.logonsFailedBuild;
        uint32 const attempts = status.logonsOk + failed;

        if (attempts == 0)
        {
            return std::string("\xE2\x80\x94");
        }

        // Integer tenths of a percent, rounded half-up. 64-bit so a long-lived
        // daemon's counters cannot overflow the scaling multiply.
        uint64 const tenths =
            (static_cast<uint64>(failed) * 1000 + attempts / 2) / attempts;

        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%u.%u%%",
            static_cast<uint32>(tenths / 10), static_cast<uint32>(tenths % 10));
        return std::string(buffer);
    }
}
