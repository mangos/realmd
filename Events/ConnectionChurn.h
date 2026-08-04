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

#ifndef MANGOS_H_CONNECTIONCHURN
#define MANGOS_H_CONNECTIONCHURN

#include "Platform/Define.h"

#include <ctime>
#include <deque>
#include <string>

namespace MaNGOS::Realmd
{
    /// Monotonic process-lifetime totals of the three churn events.
    struct ChurnTotals
    {
        uint32 accepts = 0;
        uint32 closes = 0;
        uint32 authTimeouts = 0;
    };

    /** @brief Count one accepted auth connection. Any thread; one relaxed add. */
    void RecordConnectionAccept();

    /** @brief Count one auth connection teardown. Any thread; one relaxed add. */
    void RecordConnectionClose();

    /**
     * @brief Count one connection dropped by the authentication deadline.
     *        Any thread; one relaxed add.
     */
    void RecordAuthDeadlineExpiry();

    /** @brief Read all three lifetime totals. */
    ChurnTotals LoadChurnTotals();

    /**
     * @brief Rolling per-minute view over the churn totals.
     *
     * Owned by StatusSource and therefore touched only by the housekeeping
     * thread. The auth path never sees this object -- it only bumps the three
     * atomics behind LoadChurnTotals() -- so nothing on the hot path takes a
     * lock or reads a clock.
     *
     * At most one reading is retained per distinct second, and readings older
     * than the window are evicted, so the deque never exceeds
     * windowSeconds + 1 entries. Two samples in the same second REPLACE one
     * another; when the deque holds only that one reading, the baseline moves
     * with it and the measured interval starts from the newer totals.
     */
    class ChurnWindow
    {
        public:
            explicit ChurnWindow(uint32 windowSeconds = 60);

            /**
             * @brief Fold one totals reading in.
             * @param totals the current lifetime totals
             * @param now current wall-clock time in seconds
             *
             * Safe to call every tick: readings within the same second replace
             * one another rather than accumulating, and a backwards clock step
             * discards the window rather than reporting a nonsense rate.
             */
            void Sample(ChurnTotals const& totals, time_t now);

            /// Events counted across the retained span.
            ChurnTotals Rates() const;

            /// Seconds the retained readings actually cover; 0 below two
            /// readings. May fall below windowSeconds immediately after an
            /// eviction if samples are sparser than one per second.
            uint32 SpanSeconds() const;

            /// True once the retained span covers the whole configured window.
            bool Full() const;

        private:
            struct Reading
            {
                time_t      when;
                ChurnTotals totals;
            };

            uint32 m_windowSeconds;
            std::deque<Reading> m_readings;
    };

    /**
     * @brief Render the churn window as a status-field value.
     * @return "+50/-50 \xC2\xB7 5to", prefixed with '~' while the window is
     *         still filling
     */
    std::string FormatChurnField(ChurnWindow const& window);
}

#endif
/// @}
