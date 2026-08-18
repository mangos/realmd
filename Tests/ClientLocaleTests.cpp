/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "Auth/ClientLocale.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

void CheckAccepted(std::string const& wire, char const* expected)
{
    auto const parsed = MaNGOS::Auth::ParseClientLocaleClaim(wire);
    CHECK(parsed.has_value());
    if (parsed)
    {
        CHECK(*parsed == expected);
    }
}
void CheckRejected(std::string const& wire)
{
    CHECK(!MaNGOS::Auth::ParseClientLocaleClaim(wire).has_value());
}

void TestCanonicalClaimsArePreserved()
{
    CheckAccepted("SUne", "enUS");
    CheckAccepted("BGne", "enGB");
    CheckAccepted("NChz", "zhCN");
    CheckAccepted("RBtp", "ptBR");
    CheckAccepted("URur", "ruRU");
}

void TestMalformedClaimsAreRejected()
{
    CheckRejected("");
    CheckRejected("SUn");
    CheckRejected("SUneX");
    CheckRejected("sune");
    CheckRejected("SU'X");
    CheckRejected("SU1e");
    CheckRejected(std::string("SU\0e", 4));
}
}

int main()
{
    TestCanonicalClaimsArePreserved();
    TestMalformedClaimsAreRejected();

    if (failures != 0)
    {
        std::cerr << failures << " client locale check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Client locale checks passed\n";
    return EXIT_SUCCESS;
}
