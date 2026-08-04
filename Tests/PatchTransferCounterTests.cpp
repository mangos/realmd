#include "Auth/PatchTransferCounter.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

using MaNGOS::Realmd::ActivePatchTransfers;
using MaNGOS::Realmd::CancelPatchTransfers;
using MaNGOS::Realmd::DrainPatchTransfers;
using MaNGOS::Realmd::PatchTransferGuard;

namespace
{
int failures = 0;

#define CHECK(condition)                                                        \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                 \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void TestCounterStartsIdle()
{
    CHECK(ActivePatchTransfers() == 0);
}

void TestGuardCountsExactlyOneTransfer()
{
    {
        PatchTransferGuard guard;
        CHECK(ActivePatchTransfers() == 1);
    }
    CHECK(ActivePatchTransfers() == 0);
}

void TestGuardsNest()
{
    auto first = std::make_unique<PatchTransferGuard>();
    auto second = std::make_unique<PatchTransferGuard>();
    CHECK(ActivePatchTransfers() == 2);

    second.reset();
    CHECK(ActivePatchTransfers() == 1);

    first.reset();
    CHECK(ActivePatchTransfers() == 0);
}

// Models std::thread's constructor throwing: the closure that owns the guard is
// destroyed without its body ever running, and the count must still fall.
void TestGuardIsReleasedWhenAnUnrunClosureIsDestroyed()
{
    {
        auto closure = [guard = std::make_unique<PatchTransferGuard>()]() mutable
        {
            (void)guard;
        };
        CHECK(ActivePatchTransfers() == 1);
    }
    CHECK(ActivePatchTransfers() == 0);
}

// REGISTRY semantics only. It deliberately does not model the transport: a real
// net::Closer records graceful-close intent and does not wake a producer parked
// on backpressure (net/iocp/IocpServer.cpp:52, net/reactor/ReactorServer.cpp:264
// and :359), so a test whose fake closer "released" the transfer would be
// asserting something the real one does not do. What IS asserted is what this
// file owns: each registered closer is invoked exactly once, and once the
// transfer has released its guard the entry is DEREGISTERED, not merely counted
// down.
void TestCancelReleasesALiveTransfer()
{
    std::atomic<bool> cancelled(false);
    std::atomic<bool> registered(false);

    std::thread transfer([&cancelled, &registered]()
    {
        auto guard = std::make_unique<PatchTransferGuard>(
            [&cancelled]() { cancelled.store(true); });
        registered.store(true);

        while (!cancelled.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    while (!registered.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(ActivePatchTransfers() == 1);

    CancelPatchTransfers();
    transfer.join();

    CHECK(cancelled.load());
    CHECK(ActivePatchTransfers() == 0);

    // A second cancel must find nothing at all: if the entry were only counted
    // down and not erased, this would call a closer belonging to a transfer
    // that is already gone.
    cancelled.store(false);
    CancelPatchTransfers();
    CHECK(!cancelled.load());
}
// Diagnostic, deliberately not a CHECK. An upper wall-clock bound asserts a
// property of the scheduler, not of the code: this process can be descheduled
// for a second or more on a loaded CTest run. Print the number so a genuine
// regression is visible to a human reading the log, and assert only what the
// code actually controls.
void Note(char const* what, std::chrono::steady_clock::duration elapsed)
{
    std::cout << "  note: " << what << " took "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     elapsed).count()
              << " ms\n";
}

// The 30s here is NOT an expectation, it is a deadlock fail-safe: the registry
// is empty, so a correct drain returns before it waits at all, and the only way
// to reach the deadline is a bug that never returns. It is deliberately far too
// generous to be sensitive to scheduling.
void TestDrainReturnsAtOnceWhenNothingIsRunning()
{
    auto const started = std::chrono::steady_clock::now();
    CHECK(DrainPatchTransfers(std::chrono::seconds(30)));
    Note("drain of an idle daemon", std::chrono::steady_clock::now() - started);
}

void TestDrainWithNoTimeoutStillReportsAnIdleDaemon()
{
    CHECK(DrainPatchTransfers(std::chrono::milliseconds(0)));
}

void TestDrainWithNoTimeoutReportsALiveTransfer()
{
    PatchTransferGuard guard;
    CHECK(!DrainPatchTransfers(std::chrono::milliseconds(0)));
}

// The lower bound is safe to assert: no scheduling delay can make a wait
// shorter. 150ms against a 200ms timeout keeps the slack MSVC needs, because it
// converts a relative wait_for onto a different clock and, like several pthread
// combinations, may wake slightly before the full duration has elapsed on the
// steady_clock measured here. It still distinguishes a real wait from an
// immediate false return.
void TestDrainTimesOutWhileATransferIsLive()
{
    PatchTransferGuard guard;

    auto const started = std::chrono::steady_clock::now();
    CHECK(!DrainPatchTransfers(std::chrono::milliseconds(200)));
    CHECK(std::chrono::steady_clock::now() - started >=
          std::chrono::milliseconds(150));
}

// The only test that genuinely exercises the WAIT, because the guard is still
// held when the drain is entered. The 30s is again a deadlock fail-safe and not
// an expectation: without the notify_all in ~PatchTransferGuard this call sits
// there for the full half minute and the printed note says so, but the CHECK
// itself asserts only that the drain eventually reports the daemon idle, which
// no amount of descheduling can falsify.
void TestDrainWakesWhenTheLastTransferFinishes()
{
    auto guard = std::make_unique<PatchTransferGuard>();

    std::thread releaser([guard = std::move(guard)]() mutable
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    auto const started = std::chrono::steady_clock::now();
    CHECK(DrainPatchTransfers(std::chrono::seconds(30)));
    Note("drain waiting on one transfer",
         std::chrono::steady_clock::now() - started);

    releaser.join();
    CHECK(ActivePatchTransfers() == 0);
}

// Cancel semantics, asserted WITHOUT a wall-clock bound. The transfer thread is
// JOINED first, so by the time the drain is called the guard is provably gone
// and the assertion is about the registry, not about the scheduler. An earlier
// revision used DrainPatchTransfers(5s) here, which asserted that this box
// schedules the transfer thread within five seconds -- a property of the
// machine, not of the code, and one a loaded CTest run can lose. A zero timeout
// is the stronger assertion anyway: it passes only if the registry is ALREADY
// empty, with no scheduling window to hide in.
void TestCancelThenDrainCompletes()
{
    std::atomic<bool> cancelled(false);
    std::atomic<bool> registered(false);

    std::thread transfer([&cancelled, &registered]()
    {
        auto guard = std::make_unique<PatchTransferGuard>(
            [&cancelled]() { cancelled.store(true); });
        registered.store(true);

        while (!cancelled.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    while (!registered.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto const started = std::chrono::steady_clock::now();
    CancelPatchTransfers();
    transfer.join();
    Note("cancel then join", std::chrono::steady_clock::now() - started);

    CHECK(cancelled.load());
    CHECK(DrainPatchTransfers(std::chrono::milliseconds(0)));
    CHECK(ActivePatchTransfers() == 0);
}

}

int main()
{
    TestCounterStartsIdle();
    TestGuardCountsExactlyOneTransfer();
    TestGuardsNest();
    TestGuardIsReleasedWhenAnUnrunClosureIsDestroyed();
    TestCancelReleasesALiveTransfer();
    TestDrainReturnsAtOnceWhenNothingIsRunning();
    TestDrainWithNoTimeoutStillReportsAnIdleDaemon();
    TestDrainWithNoTimeoutReportsALiveTransfer();
    TestDrainTimesOutWhileATransferIsLive();
    TestDrainWakesWhenTheLastTransferFinishes();
    TestCancelThenDrainCompletes();

    if (failures != 0)
    {
        std::cerr << failures << " patch transfer registry check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Patch transfer registry checks passed\n";
    return EXIT_SUCCESS;
}
