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
#include "Auth/AuthCounters.h"

#include <atomic>
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

using MaNGOS::Realmd::GetLogonCounts;
using MaNGOS::Realmd::LogonAttemptRecorder;
using MaNGOS::Realmd::LogonCounts;
using MaNGOS::Realmd::LogonOutcome;
using MaNGOS::Realmd::RaiseHighWaterMark;
using MaNGOS::Realmd::ResetLogonCounts;

void TestHighWaterMarkOnlyRises()
{
    std::atomic<uint32> peak{0};

    RaiseHighWaterMark(peak, 3);
    CHECK(peak.load() == 3);

    RaiseHighWaterMark(peak, 7);
    CHECK(peak.load() == 7);

    // A connection opening while the peak is already higher must not lower it.
    // This is the whole point of the helper, and the one behaviour a plain
    // store gets wrong.
    RaiseHighWaterMark(peak, 2);
    CHECK(peak.load() == 7);

    // Re-observing the mark itself changes nothing.
    RaiseHighWaterMark(peak, 7);
    CHECK(peak.load() == 7);
}


void TestEachOutcomeIncrementsOneCategory()
{
    ResetLogonCounts();

    LogonAttemptRecorder().Record(LogonOutcome::Ok);
    LogonAttemptRecorder().Record(LogonOutcome::Rejected);
    LogonAttemptRecorder().Record(LogonOutcome::BadProof);
    LogonAttemptRecorder().Record(LogonOutcome::BuildPatch);

    LogonCounts const counts = GetLogonCounts();
    CHECK(counts.ok == 1);
    CHECK(counts.rejected == 1);
    CHECK(counts.badProof == 1);
    CHECK(counts.buildPatch == 1);
    CHECK(counts.Total() == 4);
    CHECK(counts.Failures() == 3);
}

void TestConnectionContributesAtMostOneIncrement()
{
    ResetLogonCounts();

    LogonAttemptRecorder connection;
    CHECK(!connection.Recorded());

    connection.Record(LogonOutcome::Rejected);
    CHECK(connection.Recorded());

    // Whatever a later handler decides on the same connection is dropped: one
    // connection is one attempt, in whatever order the handlers run.
    connection.Record(LogonOutcome::BuildPatch);
    connection.Record(LogonOutcome::Ok);

    LogonCounts const counts = GetLogonCounts();
    CHECK(counts.Total() == 1);
    CHECK(counts.rejected == 1);
    CHECK(counts.buildPatch == 0);
    CHECK(counts.ok == 0);
}

void TestRecordersAreIndependentOfEachOther()
{
    ResetLogonCounts();

    LogonAttemptRecorder first;
    LogonAttemptRecorder second;

    first.Record(LogonOutcome::Ok);
    first.Record(LogonOutcome::Ok);
    second.Record(LogonOutcome::BadProof);
    second.Record(LogonOutcome::BadProof);

    LogonCounts const counts = GetLogonCounts();
    CHECK(counts.ok == 1);
    CHECK(counts.badProof == 1);
    CHECK(counts.Total() == 2);
}

void TestTotalsAggregateTheFourCategories()
{
    LogonCounts counts;
    counts.ok = 1247;
    counts.rejected = 20;
    counts.badProof = 15;
    counts.buildPatch = 2;

    CHECK(counts.Failures() == 37);
    CHECK(counts.Total() == 1284);

    LogonCounts const empty;
    CHECK(empty.Failures() == 0);
    CHECK(empty.Total() == 0);
}
}

int main()
{
    TestHighWaterMarkOnlyRises();
    TestEachOutcomeIncrementsOneCategory();
    TestConnectionContributesAtMostOneIncrement();
    TestRecordersAreIndependentOfEachOther();
    TestTotalsAggregateTheFourCategories();

    if (failures != 0)
    {
        std::cerr << failures << " auth counter check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Auth counter checks passed\n";
    return EXIT_SUCCESS;
}
