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

#ifndef MANGOS_H_PATCHPOLICY
#define MANGOS_H_PATCHPOLICY

#include <cstdint>
#include <optional>
#include <set>
#include <string>

class PatchPolicy
{
public:
    PatchPolicy() = default;

    static std::optional<PatchPolicy> Parse(
        bool enabled, std::string const& forcedBuilds);

    bool ShouldPatch(std::uint16_t build, bool supported) const
    {
        return m_enabled &&
            (!supported || m_forcedBuilds.count(build) != 0);
    }

private:
    bool m_enabled = true;
    std::set<std::uint16_t> m_forcedBuilds;
};

#endif
