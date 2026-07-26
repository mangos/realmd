#include "Auth/AuthCodes.h"
#include "Auth/AuthProtocolGuard.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void Check(bool condition, char const* expression, int line)
{
    if (condition)
    {
        return;
    }

    ++failures;
    std::cerr << "line " << line << ": CHECK failed: " << expression << '\n';
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

using MaNGOS::Auth::AuthChallengeHeaderSize;
using MaNGOS::Auth::AuthChallengeMinimumBodySize;
using MaNGOS::Auth::Deadline;
using MaNGOS::Auth::FrameStatus;
using MaNGOS::Auth::InspectFrame;
using MaNGOS::Auth::MaxPendingInput;
using MaNGOS::Auth::RejectReason;
using MaNGOS::Auth::StreamState;

constexpr std::size_t ChallengeAccountLengthOffset = 33;
constexpr std::size_t ChallengeAccountDataOffset = 34;
constexpr std::size_t ChallengeAccountPrefixBodySize = 30;

std::vector<std::uint8_t> MakeChallenge(std::uint8_t command,
                                        std::uint16_t build,
                                        std::string const& account = "TEST")
{
    std::uint16_t const bodySize = static_cast<std::uint16_t>(
        ChallengeAccountPrefixBodySize + account.size());
    std::vector<std::uint8_t> frame(AuthChallengeHeaderSize + bodySize, 0);
    frame[0] = command;
    frame[2] = static_cast<std::uint8_t>(bodySize & 0xFF);
    frame[3] = static_cast<std::uint8_t>(bodySize >> 8);

    if (frame.size() > 12)
    {
        frame[11] = static_cast<std::uint8_t>(build & 0xFF);
        frame[12] = static_cast<std::uint8_t>(build >> 8);
    }
    frame[ChallengeAccountLengthOffset] =
        static_cast<std::uint8_t>(account.size());
    for (std::size_t i = 0; i < account.size(); ++i)
    {
        frame[ChallengeAccountDataOffset + i] =
            static_cast<std::uint8_t>(account[i]);
    }
    return frame;
}

void SetChallengeBodySize(std::vector<std::uint8_t>& frame,
                          std::uint16_t bodySize)
{
    frame[2] = static_cast<std::uint8_t>(bodySize & 0xFF);
    frame[3] = static_cast<std::uint8_t>(bodySize >> 8);
}

void CheckChallengeSplits(std::uint8_t command)
{
    std::vector<std::uint8_t> const frame = MakeChallenge(command, 5875);
    for (std::size_t split = 0; split < frame.size(); ++split)
    {
        auto const decision =
            InspectFrame(StreamState::Challenge, frame.data(), split);
        CHECK(decision.status == FrameStatus::Incomplete);
    }

    auto const complete =
        InspectFrame(StreamState::Challenge, frame.data(), frame.size());
    CHECK(complete.status == FrameStatus::Complete);
    CHECK(complete.reason == RejectReason::None);
    CHECK(complete.frameSize == frame.size());

    std::vector<std::uint8_t> coalesced = frame;
    coalesced.push_back(CMD_AUTH_LOGON_PROOF);
    auto const first = InspectFrame(
        StreamState::Challenge, coalesced.data(), coalesced.size());
    CHECK(first.status == FrameStatus::Complete);
    CHECK(first.frameSize == frame.size());
}

void CheckFixedFrame(StreamState state, std::uint8_t command,
                     std::size_t required)
{
    std::vector<std::uint8_t> frame(required, 0);
    frame[0] = command;

    for (std::size_t split = 0; split < required; ++split)
    {
        auto const decision = InspectFrame(state, frame.data(), split);
        CHECK(decision.status == FrameStatus::Incomplete);
    }

    auto const complete = InspectFrame(state, frame.data(), frame.size());
    CHECK(complete.status == FrameStatus::Complete);
    CHECK(complete.frameSize == required);
}

void CheckProofFrame(StreamState state, std::uint8_t command,
                     std::size_t required, std::size_t keyCountOffset)
{
    CheckFixedFrame(state, command, required);

    std::vector<std::uint8_t> nonZeroKeyCount(required, 0);
    nonZeroKeyCount[0] = command;
    nonZeroKeyCount[keyCountOffset] = 1;

    auto const decision = InspectFrame(
        state, nonZeroKeyCount.data(), keyCountOffset + 1);
    CHECK(decision.status == FrameStatus::Reject);
    CHECK(decision.reason == RejectReason::UnsupportedKeyProof);
}

void CheckChallengeFraming()
{
    CheckChallengeSplits(CMD_AUTH_LOGON_CHALLENGE);
    CheckChallengeSplits(CMD_AUTH_RECONNECT_CHALLENGE);

    for (std::uint16_t body = 0;
         body < AuthChallengeMinimumBodySize; ++body)
    {
        std::vector<std::uint8_t> frame =
            MakeChallenge(CMD_AUTH_LOGON_CHALLENGE, 5875);
        SetChallengeBodySize(frame, body);
        auto const decision =
            InspectFrame(StreamState::Challenge, frame.data(), frame.size());
        CHECK(decision.status == FrameStatus::Reject);
        CHECK(decision.reason == RejectReason::MalformedLength);
    }

    std::vector<std::uint8_t> shortBody =
        MakeChallenge(CMD_AUTH_LOGON_CHALLENGE, 5875);
    SetChallengeBodySize(
        shortBody,
        static_cast<std::uint16_t>(
            ChallengeAccountPrefixBodySize +
            shortBody[ChallengeAccountLengthOffset] - 1));
    auto decision =
        InspectFrame(StreamState::Challenge, shortBody.data(), shortBody.size());
    CHECK(decision.status == FrameStatus::Reject);
    CHECK(decision.reason == RejectReason::MalformedLength);

    std::vector<std::uint8_t> longBody =
        MakeChallenge(CMD_AUTH_LOGON_CHALLENGE, 5875);
    std::uint16_t const declaredLongBody = static_cast<std::uint16_t>(
        ChallengeAccountPrefixBodySize +
        longBody[ChallengeAccountLengthOffset] + 1);
    SetChallengeBodySize(longBody, declaredLongBody);
    longBody.resize(AuthChallengeHeaderSize + declaredLongBody);
    decision =
        InspectFrame(StreamState::Challenge, longBody.data(), longBody.size());
    CHECK(decision.status == FrameStatus::Reject);
    CHECK(decision.reason == RejectReason::MalformedLength);

    std::vector<std::uint8_t> embeddedNul =
        MakeChallenge(
            CMD_AUTH_LOGON_CHALLENGE, 5875, std::string("TE\0ST", 5));
    decision = InspectFrame(
        StreamState::Challenge, embeddedNul.data(), embeddedNul.size());
    CHECK(decision.status == FrameStatus::Reject);
}

void CheckFixedFraming()
{
    CheckProofFrame(
        StreamState::LogonProof,
        CMD_AUTH_LOGON_PROOF,
        MaNGOS::Auth::AuthLogonProofSize,
        73);
    CheckProofFrame(
        StreamState::ReconnectProof,
        CMD_AUTH_RECONNECT_PROOF,
        MaNGOS::Auth::AuthReconnectProofSize,
        57);
    CheckFixedFrame(StreamState::Authenticated, CMD_REALM_LIST,
                    MaNGOS::Auth::AuthRealmListSize);
    CheckFixedFrame(StreamState::Patch, CMD_XFER_ACCEPT,
                    MaNGOS::Auth::AuthXferAcceptSize);
    CheckFixedFrame(StreamState::Patch, CMD_XFER_RESUME,
                    MaNGOS::Auth::AuthXferResumeSize);
    CheckFixedFrame(StreamState::Patch, CMD_XFER_CANCEL,
                    MaNGOS::Auth::AuthXferCancelSize);
}

void CheckRejectedCommands()
{
    std::uint8_t const unknown = 0xFF;
    auto decision =
        InspectFrame(StreamState::Challenge, &unknown, sizeof(unknown));
    CHECK(decision.status == FrameStatus::Reject);
    CHECK(decision.reason == RejectReason::UnknownCommand);

    std::uint8_t const realmList = CMD_REALM_LIST;
    decision =
        InspectFrame(StreamState::Challenge, &realmList, sizeof(realmList));
    CHECK(decision.status == FrameStatus::Reject);
    CHECK(decision.reason == RejectReason::UnauthorizedCommand);
}

void CheckPendingLimit()
{
    CHECK(MaNGOS::Auth::CanAppendPending(0, MaxPendingInput));
    CHECK(MaNGOS::Auth::CanAppendPending(MaxPendingInput, 0));
    CHECK(!MaNGOS::Auth::CanAppendPending(MaxPendingInput, 1));
    CHECK(!MaNGOS::Auth::CanAppendPending(
        std::numeric_limits<std::size_t>::max(), 1));
}

void CheckDeadline()
{
    Deadline::Clock::time_point const start{};
    Deadline deadline(start, std::chrono::seconds(30));

    CHECK(deadline.active());
    CHECK(!deadline.expired(start + std::chrono::seconds(29)));
    CHECK(deadline.expired(start + std::chrono::seconds(30)));

    deadline.deactivate();
    CHECK(!deadline.active());
    CHECK(!deadline.expired(start + std::chrono::hours(1)));

    Deadline disabled(start, std::chrono::seconds(0));
    CHECK(!disabled.active());
    CHECK(!disabled.expired(start + std::chrono::hours(1)));
}

void CheckBuildIndependentFraming()
{
    std::uint16_t const builds[] =
    {
        5875, 6005, 6141, 8606, 12340, 15595,
        18273, 18414, 21742, 26972, 35662, 40000
    };

    for (std::uint16_t build : builds)
    {
        std::vector<std::uint8_t> const frame =
            MakeChallenge(CMD_AUTH_LOGON_CHALLENGE, build);
        auto const decision =
            InspectFrame(StreamState::Challenge, frame.data(), frame.size());
        CHECK(decision.status == FrameStatus::Complete);
        CHECK(decision.frameSize == frame.size());
    }
}
}

int main()
{
    CheckChallengeFraming();
    CheckFixedFraming();
    CheckRejectedCommands();
    CheckPendingLimit();
    CheckDeadline();
    CheckBuildIndependentFraming();

    if (failures != 0)
    {
        std::cerr << failures << " auth protocol guard checks failed\n";
        return 1;
    }

    std::cout << "auth protocol guard checks passed\n";
    return 0;
}
