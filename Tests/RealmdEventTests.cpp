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
#include "Events/RealmChangeTracker.h"
#include "Events/EventFormat.h"
#include "Events/DbHealthTracker.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// Everything new in Phase 4 lives in MaNGOS::Realmd. These using-declarations
// exist so the test bodies read as they would if the symbols were global; they
// are not optional -- unqualified lookup from the global namespace finds none
// of them.
using MaNGOS::Realmd::FormatRealmChangeEvent;
using MaNGOS::Realmd::RealmChangeEvent;
using MaNGOS::Realmd::RealmChangeTracker;
using MaNGOS::Realmd::FormatShortDuration;
using MaNGOS::Realmd::DbHealthTracker;
using MaNGOS::Realmd::DbProbeState;
using MaNGOS::Realmd::LoginDbHealth;

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

RealmSnapshot MakeRealms(std::vector<std::pair<std::string, bool>> const& realms)
{
    RealmSnapshot snapshot;
    for (std::pair<std::string, bool> const& entry : realms)
    {
        Realm& realm = snapshot.realms[entry.first];
        realm.name = entry.first;
        realm.realmflags = entry.second
            ? RealmFlags(REALM_FLAG_OFFLINE) : RealmFlags(REALM_FLAG_NONE);
    }
    return snapshot;
}

void TestFirstObserveOnlyPrimes()
{
    RealmChangeTracker tracker;
    CHECK(!tracker.Primed());

    RealmSnapshot const snapshot = MakeRealms({{"One", false}, {"Two", false}});
    std::vector<RealmChangeEvent> events;
    CHECK(!tracker.Observe(snapshot, events));
    CHECK(events.empty());
    CHECK(tracker.Primed());
}

void TestShortDurationFormatting()
{
    CHECK(FormatShortDuration(-5) == "0s");
    CHECK(FormatShortDuration(0) == "0s");
    CHECK(FormatShortDuration(45) == "45s");
    CHECK(FormatShortDuration(63) == "1m03s");
    CHECK(FormatShortDuration(3600) == "1h00m");
    CHECK(FormatShortDuration(7500) == "2h05m");
    CHECK(FormatShortDuration(273120) == "3d03h");
}

LoginDbHealth MakeHealth(DbProbeState state, uint32 latencyMs,
                         time_t lastSuccess)
{
    LoginDbHealth health;
    health.state = state;
    health.latencyMs = latencyMs;
    health.lastSuccess = lastSuccess;
    return health;
}

void TestDbHealthReportsOnlyTransitions()
{
    DbHealthTracker tracker(120);
    std::string event;

    // No probe has completed yet: nothing to say.
    CHECK(!tracker.Observe(
        MakeHealth(DbProbeState::Unknown, 0, 0), 1000, event));
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_UNKNOWN);
    CHECK(event.empty());

    // First success is a transition out of "unknown".
    CHECK(tracker.Observe(MakeHealth(DbProbeState::Ok, 4, 1000), 1000, event));
    CHECK(event == "Login database probe ok (4 ms)");
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_OK);

    // A second identical reading says nothing.
    CHECK(!tracker.Observe(MakeHealth(DbProbeState::Ok, 4, 1000), 1001, event));
    CHECK(event.empty());

    // Failure once, and only once. now 1010 - lastSuccess 1001 = 9s.
    CHECK(tracker.Observe(
        MakeHealth(DbProbeState::Down, 0, 1001), 1010, event));
    CHECK(event ==
        "Login database down (probe failed; last success 9s ago)");
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_DOWN);
    CHECK(!tracker.Observe(
        MakeHealth(DbProbeState::Down, 0, 1001), 1020, event));
    CHECK(event.empty());

    // Recovery reports the outage length and the fresh probe latency.
    // Down since 1010, now 1070: held for 60s = "1m00s".
    CHECK(tracker.Observe(MakeHealth(DbProbeState::Ok, 7, 1070), 1070, event));
    CHECK(event == "Login database recovered (probe 7 ms; down for 1m00s)");
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_OK);

    // The probe stops running: 1200 - 1070 = 130 > 120, so the reading is
    // stale even though its state is still Ok. 130s = "2m10s".
    CHECK(tracker.Observe(MakeHealth(DbProbeState::Ok, 7, 1070), 1200, event));
    CHECK(event == "Login database probe stale (last success 2m10s ago)");
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_STALE);
    CHECK(!tracker.Observe(MakeHealth(DbProbeState::Ok, 7, 1070), 1201, event));

    // Stale since 1200, now 1300: 100s = "1m40s".
    CHECK(tracker.Observe(MakeHealth(DbProbeState::Ok, 5, 1300), 1300, event));
    CHECK(event ==
        "Login database probe fresh again (probe 5 ms; stale for 1m40s)");
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_OK);
}

void TestDbHealthDownBeforeAnySuccess()
{
    DbHealthTracker tracker(120);
    std::string event;
    CHECK(tracker.Observe(MakeHealth(DbProbeState::Down, 0, 0), 500, event));
    CHECK(event ==
        "Login database down (probe failed; no successful probe yet)");
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_DOWN);
}

// A backwards wall-clock step must not be mistaken for "never succeeded".
// lastSuccess is nonzero, so a probe HAS succeeded; only the elapsed time is
// unrepresentable, and it clamps to zero.
void TestDbHealthBackwardClockKeepsLastSuccess()
{
    DbHealthTracker tracker(120);
    std::string event;

    CHECK(tracker.Observe(MakeHealth(DbProbeState::Ok, 4, 5000), 5000, event));
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_OK);

    // The clock steps back behind lastSuccess before the next probe fails.
    CHECK(tracker.Observe(
        MakeHealth(DbProbeState::Down, 0, 5000), 4000, event));
    CHECK(event ==
        "Login database down (probe failed; last success 0s ago)");
    CHECK(tracker.State() == MaNGOS::Realmd::DB_HEALTH_DOWN);

    // Recovery after a backwards step reports a clamped, not negative, outage.
    CHECK(tracker.Observe(MakeHealth(DbProbeState::Ok, 6, 3900), 3900, event));
    CHECK(event == "Login database recovered (probe 6 ms; down for 0s)");
}
}

int main()
{
    TestFirstObserveOnlyPrimes();
    TestShortDurationFormatting();
    TestDbHealthReportsOnlyTransitions();
    TestDbHealthDownBeforeAnySuccess();
    TestDbHealthBackwardClockKeepsLastSuccess();

    if (failures != 0)
    {
        std::cerr << failures << " realmd event check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Realmd event checks passed\n";
    return EXIT_SUCCESS;
}
