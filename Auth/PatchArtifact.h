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

#ifndef MANGOS_H_PATCHARTIFACT
#define MANGOS_H_PATCHARTIFACT

#include <openssl/md5.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

class PatchArtifact
{
public:
    static std::unique_ptr<PatchArtifact> Open(std::string const& path);

    PatchArtifact(PatchArtifact&&) = default;
    PatchArtifact& operator=(PatchArtifact&&) = default;
    PatchArtifact(PatchArtifact const&) = delete;
    PatchArtifact& operator=(PatchArtifact const&) = delete;

    std::uint64_t size() const { return m_size; }
    std::array<std::uint8_t, MD5_DIGEST_LENGTH> const& digest() const
    {
        return m_digest;
    }

    bool Seek(std::uint64_t offset);
    std::streamsize Read(std::uint8_t* destination, std::size_t capacity);
    bool bad() const { return m_stream.bad(); }

private:
    PatchArtifact(
        std::ifstream stream,
        std::uint64_t size,
        std::array<std::uint8_t, MD5_DIGEST_LENGTH> digest);

    std::ifstream m_stream;
    std::uint64_t m_size;
    std::array<std::uint8_t, MD5_DIGEST_LENGTH> m_digest;
};

#endif
