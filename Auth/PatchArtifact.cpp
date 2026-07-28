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

#include "PatchArtifact.h"

#include "Auth/Md5.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <utility>
#include <cstring>

namespace
{
constexpr std::size_t DigestChunkSize = 4096;
}

PatchArtifact::PatchArtifact(
    std::ifstream stream,
    std::uint64_t size,
    std::array<std::uint8_t, MD5_DIGEST_LENGTH> digest)
    : m_stream(std::move(stream)),
      m_size(size),
      m_digest(std::move(digest))
{
}

std::unique_ptr<PatchArtifact> PatchArtifact::Open(std::string const& path)
{
    // A DIRECTORY IS NOT A PATCH, and only asking the filesystem says so portably. Opening
    // one with ifstream SUCCEEDS on FreeBSD and macOS and reports a plausible size, so the
    // checks below let it through and the client is served a directory as a patch file.
    // On glibc the same code happens to fail at the first read, which is why this was
    // invisible until the BSD box ran the test.
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
    {
        return nullptr;
    }

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
    {
        return nullptr;
    }

    std::streampos const end = stream.tellg();
    if (end <= std::streampos(0))
    {
        return nullptr;
    }

    std::streamoff const length = end;
    if (length <= 0 ||
        static_cast<std::uintmax_t>(length) >
            std::numeric_limits<std::uint64_t>::max())
    {
        return nullptr;
    }
    std::uint64_t const size = static_cast<std::uint64_t>(length);

    stream.seekg(0, std::ios::beg);
    if (!stream.good())
    {
        return nullptr;
    }

    // Md5Hash owns the context, so the five separate EVP_MD_CTX_free calls this
    // replaced -- one on every early return -- cannot be forgotten on a sixth.
    Md5Hash md5;

    std::array<char, DigestChunkSize> buffer{};
    while (stream)
    {
        stream.read(buffer.data(), buffer.size());
        std::streamsize const read = stream.gcount();
        if (read > 0)
        {
            md5.UpdateData(reinterpret_cast<std::uint8_t const*>(buffer.data()),
                           static_cast<std::size_t>(read));
        }
    }

    if (stream.bad())
    {
        return nullptr;
    }

    md5.Finalize();
    std::array<std::uint8_t, MD5_DIGEST_LENGTH> digest{};
    std::memcpy(digest.data(), md5.GetDigest(), digest.size());

    stream.clear();
    stream.seekg(0, std::ios::beg);
    if (!stream.good())
    {
        return nullptr;
    }

    return std::unique_ptr<PatchArtifact>(
        new PatchArtifact(std::move(stream), size, std::move(digest)));
}

bool PatchArtifact::Seek(std::uint64_t offset)
{
    if (offset > m_size ||
        offset > static_cast<std::uint64_t>(
            std::numeric_limits<std::streamoff>::max()))
    {
        return false;
    }

    m_stream.clear();
    m_stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    return m_stream.good();
}

std::streamsize PatchArtifact::Read(
    std::uint8_t* destination, std::size_t capacity)
{
    if (!destination || capacity == 0)
    {
        return 0;
    }

    std::size_t const bounded = std::min(
        capacity,
        static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max()));
    m_stream.read(
        reinterpret_cast<char*>(destination),
        static_cast<std::streamsize>(bounded));
    return m_stream.gcount();
}
