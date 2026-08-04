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
}

int main()
{
    TestCounterStartsIdle();
    TestGuardCountsExactlyOneTransfer();
    TestGuardsNest();
    TestGuardIsReleasedWhenAnUnrunClosureIsDestroyed();
    TestCancelReleasesALiveTransfer();

    if (failures != 0)
    {
        std::cerr << failures << " patch transfer registry check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Patch transfer registry checks passed\n";
    return EXIT_SUCCESS;
}
