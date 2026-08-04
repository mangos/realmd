#include "Console/RealmdStatus.h"
#include "Realm/RealmSnapshot.h"

#include <cstdlib>
#include <iostream>
#include <string>

using MaNGOS::Realmd::ApplyRealmSnapshot;
using MaNGOS::Realmd::RealmdStatus;

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

// CHECK alone reports only which line failed, which is useless when the value
// under test is a formatted string: the whole point is what it says. CHECK_EQ
// prints both sides. It is the same do/while shape and the same failure
// counter, so it remains the repository's one test convention.
#define CHECK_EQ(actual, expected)                                              \
    do                                                                          \
    {                                                                           \
        std::string const checkedActual = (actual);                             \
        std::string const checkedExpected = (expected);                         \
        if (checkedActual != checkedExpected)                                   \
        {                                                                       \
            std::cerr << __FILE__ << ':' << __LINE__ << ": expected \""         \
                      << checkedExpected << "\" but got \""                     \
                      << checkedActual << "\"\n";                               \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void TestDefaultStatusIsZeroed()
{
    RealmdStatus status;

    CHECK(status.bindIp.empty());
    CHECK(status.port == 0);
    CHECK(!status.patchEnabled);
    CHECK(status.connections == 0);
    CHECK(status.authWaiting == 0);
    CHECK(status.peakConnections == 0);
    CHECK(status.realmsTotal == 0);
    CHECK(status.realmsOnline == 0);
    CHECK(status.snapshotAgeSeconds == 0);
    CHECK(!status.snapshotPublished);
    CHECK(status.logonsOk == 0);
    CHECK(status.logonsFailedRejected == 0);
    CHECK(status.logonsFailedBadProof == 0);
    CHECK(status.logonsFailedBuild == 0);
    CHECK(status.patchTransfersActive == 0);
    CHECK(!status.dbProbed);
    CHECK(!status.dbOk);
    CHECK(status.dbLatencyMs == 0);
    CHECK(status.uptimeSeconds == 0);
    CHECK(status.churn.empty());
    CHECK(status.scheduledExit.empty());
}

void AddRealm(RealmSnapshot& snapshot, std::string const& name, RealmFlags flags)
{
    Realm& realm = snapshot.realms[name];
    realm.name = name;
    realm.realmflags = flags;
}

void TestRealmSnapshotSummary()
{
    RealmSnapshot snapshot;
    AddRealm(snapshot, "One", REALM_FLAG_NONE);
    AddRealm(snapshot, "Two", REALM_FLAG_OFFLINE);
    AddRealm(snapshot, "Three", REALM_FLAG_RECOMMENDED);
    snapshot.publishedAt = 1000;

    RealmdStatus status;
    ApplyRealmSnapshot(snapshot, 1185, status);

    CHECK(status.realmsTotal == 3);
    CHECK(status.realmsOnline == 2);
    CHECK(status.snapshotPublished);
    CHECK(status.snapshotAgeSeconds == 185);
}

void TestUnpublishedSnapshotHasNoAge()
{
    RealmSnapshot snapshot;

    RealmdStatus status;
    ApplyRealmSnapshot(snapshot, 1185, status);

    CHECK(status.realmsTotal == 0);
    CHECK(status.realmsOnline == 0);
    CHECK(!status.snapshotPublished);
    CHECK(status.snapshotAgeSeconds == 0);
}

void TestSnapshotAgeIgnoresBackwardsClock()
{
    RealmSnapshot snapshot;
    snapshot.publishedAt = 2000;

    RealmdStatus status;
    ApplyRealmSnapshot(snapshot, 1000, status);

    CHECK(status.snapshotPublished);
    CHECK(status.snapshotAgeSeconds == 0);
}
}

int main()
{
    TestDefaultStatusIsZeroed();
    TestRealmSnapshotSummary();
    TestUnpublishedSnapshotHasNoAge();
    TestSnapshotAgeIgnoresBackwardsClock();

    if (failures != 0)
    {
        std::cerr << failures << " realmd console check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Realmd console checks passed\n";
    return EXIT_SUCCESS;
}
