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

#include "AuthProtocolGuard.h"

#include "AuthCodes.h"

namespace MaNGOS::Auth
{
namespace
{
FrameDecision Reject(RejectReason reason)
{
    return {FrameStatus::Reject, reason, 0};
}

bool IsState(StreamState actual, StreamState expected)
{
    return actual == expected;
}

constexpr std::size_t ChallengeAccountPrefixBodySize = 30;
constexpr std::size_t ChallengeAccountLengthOffset =
    AuthChallengeHeaderSize + ChallengeAccountPrefixBodySize - 1;
constexpr std::size_t ChallengeAccountDataOffset =
    ChallengeAccountLengthOffset + 1;
constexpr std::size_t LogonProofKeyCountOffset = 73;
constexpr std::size_t ReconnectProofKeyCountOffset = 57;
}

FrameDecision InspectFrame(
    StreamState state, std::uint8_t const* data, std::size_t size)
{
    if (size == 0)
    {
        return {FrameStatus::Incomplete, RejectReason::None, 0};
    }

    if (!data)
    {
        return Reject(RejectReason::MalformedLength);
    }

    std::size_t required = 0;
    switch (data[0])
    {
        case CMD_AUTH_LOGON_CHALLENGE:
        case CMD_AUTH_RECONNECT_CHALLENGE:
        {
            if (!IsState(state, StreamState::Challenge))
            {
                return Reject(RejectReason::UnauthorizedCommand);
            }

            if (size < AuthChallengeHeaderSize)
            {
                return
                {
                    FrameStatus::Incomplete,
                    RejectReason::None,
                    AuthChallengeHeaderSize
                };
            }

            std::uint16_t const body =
                static_cast<std::uint16_t>(data[2]) |
                (static_cast<std::uint16_t>(data[3]) << 8);
            if (body < AuthChallengeMinimumBodySize)
            {
                return Reject(RejectReason::MalformedLength);
            }

            required = AuthChallengeHeaderSize + body;
            if (size < required)
            {
                return {FrameStatus::Incomplete, RejectReason::None, required};
            }

            std::uint8_t const accountLength =
                data[ChallengeAccountLengthOffset];
            if (body != ChallengeAccountPrefixBodySize + accountLength)
            {
                return Reject(RejectReason::MalformedLength);
            }

            // Every C0 control byte and DEL, not merely NUL. The account name
            // is interpolated into the lookup statement (AuthSocket.cpp:617)
            // and DatabaseMysql logs the whole statement when a query fails,
            // so a control byte here reaches whatever the log sink happens to
            // be. With Console.Style plain -- or a writer whose UI is not
            // active -- that sink is a raw fwrite to the operator's terminal,
            // which never passes through ConsoleUI::Sanitise. An ESC would
            // then be a terminal control sequence rather than text.
            //
            // Refusing them at the boundary costs nothing: no account name
            // legitimately contains a control character, and one that does
            // cannot match a row in `account` anyway. Doing it here rather
            // than at every sink means the bytes never enter the process.
            for (std::size_t i = 0; i < accountLength; ++i)
            {
                std::uint8_t const ch = data[ChallengeAccountDataOffset + i];
                if (ch < 0x20 || ch == 0x7F)
                {
                    return Reject(RejectReason::MalformedLength);
                }
            }

            return {FrameStatus::Complete, RejectReason::None, required};
        }
        case CMD_AUTH_LOGON_PROOF:
        {
            if (!IsState(state, StreamState::LogonProof))
            {
                return Reject(RejectReason::UnauthorizedCommand);
            }
            if (size <= LogonProofKeyCountOffset)
            {
                return
                {
                    FrameStatus::Incomplete,
                    RejectReason::None,
                    LogonProofKeyCountOffset + 1
                };
            }
            if (data[LogonProofKeyCountOffset] != 0)
            {
                return Reject(RejectReason::UnsupportedKeyProof);
            }
            required = AuthLogonProofSize;
            break;
        }
        case CMD_AUTH_RECONNECT_PROOF:
        {
            if (!IsState(state, StreamState::ReconnectProof))
            {
                return Reject(RejectReason::UnauthorizedCommand);
            }
            if (size <= ReconnectProofKeyCountOffset)
            {
                return
                {
                    FrameStatus::Incomplete,
                    RejectReason::None,
                    ReconnectProofKeyCountOffset + 1
                };
            }
            if (data[ReconnectProofKeyCountOffset] != 0)
            {
                return Reject(RejectReason::UnsupportedKeyProof);
            }
            required = AuthReconnectProofSize;
            break;
        }
        case CMD_REALM_LIST:
        {
            if (!IsState(state, StreamState::Authenticated))
            {
                return Reject(RejectReason::UnauthorizedCommand);
            }
            required = AuthRealmListSize;
            break;
        }
        case CMD_XFER_ACCEPT:
        {
            if (!IsState(state, StreamState::Patch))
            {
                return Reject(RejectReason::UnauthorizedCommand);
            }
            required = AuthXferAcceptSize;
            break;
        }
        case CMD_XFER_RESUME:
        {
            if (!IsState(state, StreamState::Patch))
            {
                return Reject(RejectReason::UnauthorizedCommand);
            }
            required = AuthXferResumeSize;
            break;
        }
        case CMD_XFER_CANCEL:
        {
            if (!IsState(state, StreamState::Patch))
            {
                return Reject(RejectReason::UnauthorizedCommand);
            }
            required = AuthXferCancelSize;
            break;
        }
        default:
        {
            return Reject(RejectReason::UnknownCommand);
        }
    }

    if (size < required)
    {
        return {FrameStatus::Incomplete, RejectReason::None, required};
    }

    return {FrameStatus::Complete, RejectReason::None, required};
}

bool CanAppendPending(std::size_t pending, std::size_t incoming)
{
    return pending <= MaxPendingInput &&
           incoming <= MaxPendingInput - pending;
}

Deadline::Deadline(Clock::time_point start, std::chrono::seconds timeout)
    : m_deadline(start + timeout), m_active(timeout.count() > 0)
{
}

bool Deadline::expired(Clock::time_point now) const
{
    return m_active.load(std::memory_order_acquire) && now >= m_deadline;
}

void Deadline::deactivate()
{
    m_active.store(false, std::memory_order_release);
}

bool Deadline::active() const
{
    return m_active.load(std::memory_order_acquire);
}
}
