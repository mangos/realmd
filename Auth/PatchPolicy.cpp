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
 */

#include "PatchPolicy.h"

#include <charconv>
#include <limits>
#include <sstream>
#include <system_error>

std::optional<PatchPolicy> PatchPolicy::Parse(
    bool enabled, std::string const& forcedBuilds)
{
    PatchPolicy policy;
    policy.m_enabled = enabled;

    std::istringstream tokens(forcedBuilds);
    std::string token;
    while (tokens >> token)
    {
        std::uint32_t build = 0;
        auto const parsed =
            std::from_chars(token.data(), token.data() + token.size(), build);
        if (parsed.ec != std::errc() ||
            parsed.ptr != token.data() + token.size() ||
            build == 0 ||
            build > std::numeric_limits<std::uint16_t>::max())
        {
            return std::nullopt;
        }

        policy.m_forcedBuilds.insert(static_cast<std::uint16_t>(build));
    }

    return policy;
}
