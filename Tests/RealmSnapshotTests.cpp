#include "Realm/RealmSnapshot.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

std::shared_ptr<RealmSnapshot> MakeSnapshot(std::string const& name, uint32 id)
{
    auto snapshot = std::make_shared<RealmSnapshot>();
    auto [realm, inserted] = snapshot->realms.emplace(name, Realm{});
    CHECK(inserted);
    realm->second.name = name;
    realm->second.m_ID = id;
    snapshot->realmsByVersion[REALM_VERSION_VANILLA].push_back(&realm->second);
    return snapshot;
}

void TestViewRetainsPublishedGeneration()
{
    RealmSnapshotStore store;
    store.Publish(MakeSnapshot("First", 1));
    RealmListView oldView(store.Load(), REALM_VERSION_VANILLA);

    store.Publish(MakeSnapshot("Second", 2));

    CHECK(oldView.size() == 1);
    CHECK((*oldView.begin())->name == "First");

    RealmListView newView(store.Load(), REALM_VERSION_VANILLA);
    CHECK(newView.size() == 1);
    CHECK((*newView.begin())->name == "Second");
}

void TestConcurrentPublicationIsConsistent()
{
    RealmSnapshotStore store;
    store.Publish(MakeSnapshot("Realm-0", 0));

    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<int> inconsistencies{0};

    std::thread publisher([&]
    {
        while (!start.load(std::memory_order_acquire))
        {
        }

        for (uint32 generation = 1; generation <= 2000; ++generation)
        {
            store.Publish(MakeSnapshot(
                "Realm-" + std::to_string(generation), generation));
        }
        done.store(true, std::memory_order_release);
    });

    std::vector<std::thread> readers;
    for (int reader = 0; reader < 4; ++reader)
    {
        readers.emplace_back([&]
        {
            start.store(true, std::memory_order_release);
            do
            {
                RealmListView view(store.Load(), REALM_VERSION_VANILLA);
                if (view.size() != 1)
                {
                    inconsistencies.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                Realm const* realm = *view.begin();
                std::string const expected =
                    "Realm-" + std::to_string(realm->m_ID);
                if (realm->name != expected)
                {
                    inconsistencies.fetch_add(1, std::memory_order_relaxed);
                }
            }
            while (!done.load(std::memory_order_acquire));
        });
    }

    publisher.join();
    for (std::thread& reader : readers)
    {
        reader.join();
    }

    CHECK(inconsistencies.load() == 0);
}

void TestRefreshGateAdmitsOneCaller()
{
    RealmRefreshGate gate(20, 100);
    std::atomic<int> entered{0};
    std::vector<std::thread> callers;
    for (int i = 0; i < 8; ++i)
    {
        callers.emplace_back([&]
        {
            gate.RunIfDue(100, [&] { entered.fetch_add(1); });
        });
    }
    for (std::thread& caller : callers)
    {
        caller.join();
    }
    CHECK(entered.load() == 1);
}

void TestRefreshGateDoesNotBlockOtherCallers()
{
    RealmRefreshGate gate(20, 100);
    std::promise<void> refreshEntered;
    std::promise<void> releaseRefresh;
    std::future<void> release = releaseRefresh.get_future();

    std::thread owner([&]
    {
        gate.RunIfDue(100, [&]
        {
            refreshEntered.set_value();
            release.wait();
        });
    });
    refreshEntered.get_future().wait();

    std::future<bool> concurrent = std::async(std::launch::async, [&]
    {
        return gate.RunIfDue(100, [] {});
    });

    bool const returnedImmediately =
        concurrent.wait_for(std::chrono::milliseconds(100)) ==
        std::future_status::ready;
    CHECK(returnedImmediately);

    releaseRefresh.set_value();
    owner.join();
    CHECK(!concurrent.get());
}
}

int main()
{
    TestViewRetainsPublishedGeneration();
    TestConcurrentPublicationIsConsistent();
    TestRefreshGateAdmitsOneCaller();
    TestRefreshGateDoesNotBlockOtherCallers();

    if (failures != 0)
    {
        std::cerr << failures << " realm snapshot check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Realm snapshot checks passed\n";
    return EXIT_SUCCESS;
}
