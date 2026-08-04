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

#include "Events/ConnectionChurn.h"

#include <atomic>
#include <cstdio>

namespace
{
    // Namespace scope and constant-initialised, so these outlive every
    // AuthSocket, the AuthServer and the console, and are valid before any
    // dynamic initialisation runs. Plain atomics need no leak-on-purpose
    // protection: unlike the patch-transfer state in Auth/PatchTransferCounter,
    // there is no destructor for a late writer to run into, which is also why
    // the console lifecycle's std::_Exit path cannot run into them.
    std::atomic<uint32> s_accepts{0};
    std::atomic<uint32> s_closes{0};
    std::atomic<uint32> s_authTimeouts{0};
}

namespace MaNGOS::Realmd
{
    void RecordConnectionAccept()
    {
        s_accepts.fetch_add(1, std::memory_order_relaxed);
    }

    void RecordConnectionClose()
    {
        s_closes.fetch_add(1, std::memory_order_relaxed);
    }

    void RecordAuthDeadlineExpiry()
    {
        s_authTimeouts.fetch_add(1, std::memory_order_relaxed);
    }

    ChurnTotals LoadChurnTotals()
    {
        ChurnTotals totals;
        totals.accepts = s_accepts.load(std::memory_order_relaxed);
        totals.closes = s_closes.load(std::memory_order_relaxed);
        totals.authTimeouts = s_authTimeouts.load(std::memory_order_relaxed);
        return totals;
    }

    ChurnWindow::ChurnWindow(uint32 windowSeconds)
        : m_windowSeconds(windowSeconds ? windowSeconds : 60)
    {
    }

    void ChurnWindow::Sample(ChurnTotals const& totals, time_t now)
    {
        if (!m_readings.empty() && m_readings.back().when == now)
        {
            // Same second: the newer reading is the better reading for this
            // second, so it replaces the older one. If this is the only reading
            // it is also the baseline, which therefore moves with it.
            m_readings.back().totals = totals;
        }
        else
        {
            if (!m_readings.empty() && m_readings.back().when > now)
            {
                // The wall clock stepped backwards; every retained span is now
                // meaningless, so restart rather than report a negative rate.
                m_readings.clear();
            }

            Reading reading;
            reading.when = now;
            reading.totals = totals;
            m_readings.push_back(reading);
        }

        time_t const oldest = now - time_t(m_windowSeconds);
        while (m_readings.size() > 1 && m_readings.front().when < oldest)
        {
            m_readings.pop_front();
        }
    }

    ChurnTotals ChurnWindow::Rates() const
    {
        ChurnTotals rates;
        if (m_readings.size() < 2)
        {
            return rates;
        }

        ChurnTotals const& first = m_readings.front().totals;
        ChurnTotals const& last = m_readings.back().totals;

        // Unsigned subtraction, so a wrapped lifetime total still gives the
        // right difference.
        rates.accepts = last.accepts - first.accepts;
        rates.closes = last.closes - first.closes;
        rates.authTimeouts = last.authTimeouts - first.authTimeouts;
        return rates;
    }

    uint32 ChurnWindow::SpanSeconds() const
    {
        if (m_readings.size() < 2)
        {
            return 0;
        }

        return uint32(m_readings.back().when - m_readings.front().when);
    }

    bool ChurnWindow::Full() const
    {
        return SpanSeconds() >= m_windowSeconds;
    }

    std::string FormatChurnField(ChurnWindow const& window)
    {
        ChurnTotals const rates = window.Rates();

        char text[64];
        // U+00B7 written as explicit UTF-8 bytes so this file stays ASCII.
        snprintf(text, sizeof(text), "+%u/-%u \xC2\xB7 %uto",
                 rates.accepts, rates.closes, rates.authTimeouts);

        std::string field;
        if (!window.Full())
        {
            field = "~";
        }

        field += text;
        return field;
    }
}
