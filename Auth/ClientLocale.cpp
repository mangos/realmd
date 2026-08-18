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

#include <utility>

namespace MaNGOS::Auth
{
std::optional<std::string> ParseClientLocaleClaim(std::string_view wireLocale)
{
    if (wireLocale.size() != 4)
    {
        return std::nullopt;
    }

    std::string locale(wireLocale.rbegin(), wireLocale.rend());
    bool const canonical =
        locale[0] >= 'a' && locale[0] <= 'z' &&
        locale[1] >= 'a' && locale[1] <= 'z' &&
        locale[2] >= 'A' && locale[2] <= 'Z' &&
        locale[3] >= 'A' && locale[3] <= 'Z';
    return canonical ? std::optional<std::string>(std::move(locale)) :
        std::nullopt;
}
}
