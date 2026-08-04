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
#include "Events/ConnectionChurn.h"
#include "Events/ScheduledExitCountdown.h"
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
using MaNGOS::Realmd::FormatScheduledExitCountdown;
using MaNGOS::Realmd::ScheduledExitSecondsRemaining;
using MaNGOS::Realmd::ChurnTotals;
using MaNGOS::Realmd::ChurnWindow;
using MaNGOS::Realmd::FormatChurnField;
using MaNGOS::Realmd::LoadChurnTotals;
using MaNGOS::Realmd::RecordAuthDeadlineExpiry;
using MaNGOS::Realmd::RecordConnectionAccept;
using MaNGOS::Realmd::RecordConnectionClose;

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

std::tm MakeLocalTime(int wday, int hour, int minute, int second)
{
    std::tm localTime = std::tm();
    localTime.tm_wday = wday;
    localTime.tm_hour = hour;
    localTime.tm_min = minute;
    localTime.tm_sec = second;
    return localTime;
}

MaNGOS::ScheduledExitSchedule MakeSchedule(
    bool enabled, uint32 dayOfWeek, uint32 hour, uint32 minute,
    MaNGOS::ScheduledExitMode mode)
{
    MaNGOS::ScheduledExitSchedule schedule;
    schedule.enabled = enabled;
    schedule.dayOfWeek = dayOfWeek;
    schedule.hour = hour;
    schedule.minute = minute;
    schedule.mode = mode;
    return schedule;
}

void TestScheduledExitCountdown()
{
    MaNGOS::ScheduledExitSchedule const disabled = MakeSchedule(
        false, 3, 5, 0, MaNGOS::SCHEDULED_EXIT_MODE_RESTART);
    CHECK(ScheduledExitSecondsRemaining(
        disabled, MakeLocalTime(3, 4, 0, 0)) == -1);
    CHECK(FormatScheduledExitCountdown(
        disabled, MakeLocalTime(3, 4, 0, 0)).empty());

    // Wednesday 05:00 restart, seen from Wednesday 04:47:30.
    // fireOfDay 18000 - nowOfDay 17250 = 750 = 12m30s.
    MaNGOS::ScheduledExitSchedule const restart = MakeSchedule(
        true, 3, 5, 0, MaNGOS::SCHEDULED_EXIT_MODE_RESTART);
    CHECK(ScheduledExitSecondsRemaining(
        restart, MakeLocalTime(3, 4, 47, 30)) == 750);
    CHECK(FormatScheduledExitCountdown(
        restart, MakeLocalTime(3, 4, 47, 30)) == "Restart in 12m30s");

    // Outside the threshold nothing is shown, even though it is still due.
    CHECK(ScheduledExitSecondsRemaining(
        restart, MakeLocalTime(3, 2, 0, 0)) == 10800);
    CHECK(FormatScheduledExitCountdown(
        restart, MakeLocalTime(3, 2, 0, 0)).empty());

    // A day already past this week rolls forward to next week:
    // daysAhead = 3 - 4 = -1, +7 = 6, same time of day.
    CHECK(ScheduledExitSecondsRemaining(
        restart, MakeLocalTime(4, 5, 0, 0)) == 6 * 86400);

    // Same day, fire time already 30s gone by: -30 + one week.
    CHECK(ScheduledExitSecondsRemaining(
        restart, MakeLocalTime(3, 5, 0, 30)) == 7 * 86400 - 30);

    // Shutdown mode names itself. 84600 - 84555 = 45.
    MaNGOS::ScheduledExitSchedule const shutdown = MakeSchedule(
        true, 0, 23, 30, MaNGOS::SCHEDULED_EXIT_MODE_SHUTDOWN);
    CHECK(FormatScheduledExitCountdown(
        shutdown, MakeLocalTime(0, 23, 29, 15)) == "Shutdown in 45s");
}

ChurnTotals MakeTotals(uint32 accepts, uint32 closes, uint32 authTimeouts)
{
    ChurnTotals totals;
    totals.accepts = accepts;
    totals.closes = closes;
    totals.authTimeouts = authTimeouts;
    return totals;
}

void TestChurnCountersAreMonotonic()
{
    ChurnTotals const before = LoadChurnTotals();
    RecordConnectionAccept();
    RecordConnectionAccept();
    RecordConnectionClose();
    RecordAuthDeadlineExpiry();
    ChurnTotals const after = LoadChurnTotals();

    CHECK(after.accepts - before.accepts == 2);
    CHECK(after.closes - before.closes == 1);
    CHECK(after.authTimeouts - before.authTimeouts == 1);
}

// Every expectation below is the arithmetic Sample() actually performs. The
// deque state is written out at each step so a reader can check it without
// running the code.
void TestChurnWindowRates()
{
    ChurnWindow window(60);

    // [ {1000,(100,90,3)} ] -- one reading proves nothing yet.
    window.Sample(MakeTotals(100, 90, 3), 1000);
    CHECK(window.SpanSeconds() == 0);
    CHECK(!window.Full());
    CHECK(window.Rates().accepts == 0);

    // Same second: the reading is REPLACED, not appended. The window now holds
    // exactly [ {1000,(104,92,3)} ], so (104,92,3) is also the new baseline.
    window.Sample(MakeTotals(104, 92, 3), 1000);
    CHECK(window.SpanSeconds() == 0);
    CHECK(window.Rates().accepts == 0);

    // [ {1000,(104,92,3)}, {1030,(112,99,6)} ]
    // oldest = 1030 - 60 = 970; front 1000 is not < 970, so nothing is evicted.
    // Rates = 112-104, 99-92, 6-3 = 8, 7, 3. Span = 1030 - 1000 = 30.
    window.Sample(MakeTotals(112, 99, 6), 1030);
    CHECK(window.SpanSeconds() == 30);
    CHECK(!window.Full());
    CHECK(window.Rates().accepts == 8);
    CHECK(window.Rates().closes == 7);
    CHECK(window.Rates().authTimeouts == 3);
    CHECK(FormatChurnField(window) == "~+8/-7 \xC2\xB7 3to");

    // [ {1000,(104,92,3)}, {1030,(112,99,6)}, {1060,(150,140,8)} ]
    // oldest = 1060 - 60 = 1000; front 1000 is not < 1000, so nothing is
    // evicted. Rates = 150-104, 140-92, 8-3 = 46, 48, 5. Span = 60, so the
    // window is Full and the '~' prefix drops.
    window.Sample(MakeTotals(150, 140, 8), 1060);
    CHECK(window.SpanSeconds() == 60);
    CHECK(window.Full());
    CHECK(window.Rates().accepts == 46);
    CHECK(window.Rates().closes == 48);
    CHECK(FormatChurnField(window) == "+46/-48 \xC2\xB7 5to");

    // oldest = 1061 - 60 = 1001; front 1000 < 1001, so the 1000 reading is
    // evicted and the front becomes 1030.
    // [ {1030,(112,99,6)}, {1060,(150,140,8)}, {1061,(151,141,8)} ]
    // Rates = 151-112 = 39. Span = 1061 - 1030 = 31, NOT 60: with samples this
    // sparse the retained span shrinks below the window after an eviction. The
    // daemon samples once per second, so in production the deque is continuous
    // and the span sits at 60 permanently.
    window.Sample(MakeTotals(151, 141, 8), 1061);
    CHECK(window.SpanSeconds() == 31);
    CHECK(!window.Full());
    CHECK(window.Rates().accepts == 39);
}

void TestChurnWindowSurvivesClockGoingBackwards()
{
    ChurnWindow window(60);
    window.Sample(MakeTotals(10, 5, 0), 2000);
    window.Sample(MakeTotals(20, 15, 1), 2030);
    CHECK(window.SpanSeconds() == 30);

    // Wall clock steps back: the retained span is meaningless, so start again.
    window.Sample(MakeTotals(21, 16, 1), 1500);
    CHECK(window.SpanSeconds() == 0);
    CHECK(window.Rates().accepts == 0);
}

void TestChurnTotalsWrapCorrectly()
{
    // 5u - 0xFFFFFFF0u wraps to 21 in unsigned arithmetic: 16 counts to reach
    // the wrap plus 5 after it.
    ChurnWindow window(60);
    window.Sample(MakeTotals(0xFFFFFFF0u, 0, 0), 3000);
    window.Sample(MakeTotals(0x00000005u, 0, 0), 3010);
    CHECK(window.Rates().accepts == 21);
}
}

int main()
{
    TestFirstObserveOnlyPrimes();
    TestChurnCountersAreMonotonic();
    TestChurnWindowRates();
    TestChurnWindowSurvivesClockGoingBackwards();
    TestChurnTotalsWrapCorrectly();
    TestScheduledExitCountdown();
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
