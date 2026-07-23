#include "Auth/AuthCodes.h"
#include "Auth/AuthProtocolGuard.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
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

std::vector<std::uint8_t> MakeChallenge(std::uint8_t command,
                                        std::uint16_t build,
                                        std::uint16_t bodySize =
                                            AuthChallengeMinimumBodySize)
{
    std::vector<std::uint8_t> frame(AuthChallengeHeaderSize + bodySize, 0);
    frame[0] = command;
    frame[2] = static_cast<std::uint8_t>(bodySize & 0xFF);
    frame[3] = static_cast<std::uint8_t>(bodySize >> 8);

    if (frame.size() > 12)
    {
        frame[11] = static_cast<std::uint8_t>(build & 0xFF);
        frame[12] = static_cast<std::uint8_t>(build >> 8);
    }
    return frame;
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

void CheckChallengeFraming()
{
    CheckChallengeSplits(CMD_AUTH_LOGON_CHALLENGE);
    CheckChallengeSplits(CMD_AUTH_RECONNECT_CHALLENGE);

    for (std::uint16_t body = 0;
         body < AuthChallengeMinimumBodySize; ++body)
    {
        std::vector<std::uint8_t> const frame =
            MakeChallenge(CMD_AUTH_LOGON_CHALLENGE, 5875, body);
        auto const decision =
            InspectFrame(StreamState::Challenge, frame.data(), frame.size());
        CHECK(decision.status == FrameStatus::Reject);
        CHECK(decision.reason == RejectReason::MalformedLength);
    }
}

void CheckFixedFraming()
{
    CheckFixedFrame(StreamState::LogonProof, CMD_AUTH_LOGON_PROOF,
                    MaNGOS::Auth::AuthLogonProofSize);
    CheckFixedFrame(StreamState::ReconnectProof, CMD_AUTH_RECONNECT_PROOF,
                    MaNGOS::Auth::AuthReconnectProofSize);
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
