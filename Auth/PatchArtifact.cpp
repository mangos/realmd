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

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

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

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context)
    {
        return nullptr;
    }

    if (EVP_DigestInit_ex(context, EVP_md5(), nullptr) != 1)
    {
        EVP_MD_CTX_free(context);
        return nullptr;
    }

    std::array<char, DigestChunkSize> buffer{};
    while (stream)
    {
        stream.read(buffer.data(), buffer.size());
        std::streamsize const read = stream.gcount();
        if (read > 0 &&
            EVP_DigestUpdate(
                context, buffer.data(), static_cast<std::size_t>(read)) != 1)
        {
            EVP_MD_CTX_free(context);
            return nullptr;
        }
    }

    if (stream.bad())
    {
        EVP_MD_CTX_free(context);
        return nullptr;
    }

    std::array<std::uint8_t, MD5_DIGEST_LENGTH> digest{};
    unsigned int digestLength = 0;
    bool const finalized =
        EVP_DigestFinal_ex(context, digest.data(), &digestLength) == 1;
    EVP_MD_CTX_free(context);
    if (!finalized || digestLength != digest.size())
    {
        return nullptr;
    }

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
