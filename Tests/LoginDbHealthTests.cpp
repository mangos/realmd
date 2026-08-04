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
#include "Console/LoginDbHealth.h"

#include <cstdlib>
#include <iostream>

namespace
{
int failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

using MaNGOS::Realmd::DbProbeState;
using MaNGOS::Realmd::GetLoginDbHealth;
using MaNGOS::Realmd::LoginDbHealth;
using MaNGOS::Realmd::RecordLoginDbProbe;
using MaNGOS::Realmd::ResetLoginDbHealth;

void TestUnprobedIsUnknownNotOk()
{
    LoginDbHealth const health;

    // An unprobed database must never read as healthy. StatusSource::Gather
    // maps Unknown onto dbProbed = false, which is what makes FormatHeaderRight
    // render "DB \xE2\x80\x94 " rather than "DB ok 0ms".
    CHECK(health.state == DbProbeState::Unknown);
    CHECK(health.latencyMs == 0);
    CHECK(health.lastSuccess == 0);

    ResetLoginDbHealth();
    CHECK(GetLoginDbHealth().state == DbProbeState::Unknown);
}

void TestSuccessfulProbeRecordsLatencyAndTime()
{
    LoginDbHealth health;
    RecordLoginDbProbe(health, true, 4, 1000);

    CHECK(health.state == DbProbeState::Ok);
    CHECK(health.latencyMs == 4);
    CHECK(health.lastSuccess == 1000);
}

void TestFailedProbeKeepsLastSuccess()
{
    LoginDbHealth health;
    RecordLoginDbProbe(health, true, 4, 1000);
    RecordLoginDbProbe(health, false, 250, 1012);

    CHECK(health.state == DbProbeState::Down);
    CHECK(health.latencyMs == 250);

    // Kept, not cleared: phase 4's DbHealthTracker needs it to say how long the
    // outage has been running.
    CHECK(health.lastSuccess == 1000);
}

void TestNeverSucceededHasNoLastSuccess()
{
    LoginDbHealth health;
    RecordLoginDbProbe(health, false, 30, 500);

    CHECK(health.state == DbProbeState::Down);
    CHECK(health.lastSuccess == 0);
}

void TestProcessWideRecordTracksProbes()
{
    ResetLoginDbHealth();
    RecordLoginDbProbe(true, 7, 2000);

    LoginDbHealth const health = GetLoginDbHealth();
    CHECK(health.state == DbProbeState::Ok);
    CHECK(health.latencyMs == 7);
    CHECK(health.lastSuccess == 2000);
}
}

int main()
{
    TestUnprobedIsUnknownNotOk();
    TestSuccessfulProbeRecordsLatencyAndTime();
    TestFailedProbeKeepsLastSuccess();
    TestNeverSucceededHasNoLastSuccess();
    TestProcessWideRecordTracksProbes();

    if (failures != 0)
    {
        std::cerr << failures << " login database health check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Login database health checks passed\n";
    return EXIT_SUCCESS;
}
