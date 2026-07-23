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

#ifndef MANGOS_H_AUTHPROTOCOLGUARD
#define MANGOS_H_AUTHPROTOCOLGUARD

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace MaNGOS::Auth
{
enum class StreamState
{
    Challenge,
    LogonProof,
    ReconnectProof,
    Patch,
    Authenticated,
    Closed
};

enum class FrameStatus
{
    Incomplete,
    Complete,
    Reject
};

enum class RejectReason
{
    None,
    UnknownCommand,
    UnauthorizedCommand,
    MalformedLength
};

struct FrameDecision
{
    FrameStatus status;
    RejectReason reason;
    std::size_t frameSize;
};

constexpr std::size_t AuthChallengeHeaderSize = 4;
constexpr std::size_t AuthChallengeMinimumBodySize = 31;
constexpr std::size_t AuthLogonProofSize = 75;
constexpr std::size_t AuthReconnectProofSize = 58;
constexpr std::size_t AuthRealmListSize = 5;
constexpr std::size_t AuthXferAcceptSize = 1;
constexpr std::size_t AuthXferResumeSize = 9;
constexpr std::size_t AuthXferCancelSize = 1;
constexpr std::size_t MaxPendingInput =
    AuthChallengeHeaderSize + std::numeric_limits<std::uint16_t>::max();

FrameDecision InspectFrame(
    StreamState state, std::uint8_t const* data, std::size_t size);

bool CanAppendPending(std::size_t pending, std::size_t incoming);

class Deadline
{
    public:
        using Clock = std::chrono::steady_clock;

        Deadline(Clock::time_point start, std::chrono::seconds timeout);

        bool expired(Clock::time_point now) const;
        void deactivate();
        bool active() const;

    private:
        Clock::time_point m_deadline;
        std::atomic<bool> m_active;
};
}

#endif
