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
}

int main()
{
    TestFirstObserveOnlyPrimes();

    if (failures != 0)
    {
        std::cerr << failures << " realmd event check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Realmd event checks passed\n";
    return EXIT_SUCCESS;
}
