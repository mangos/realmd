#include "Console/RealmdStatus.h"
#include "Auth/AccountNameFold.h"
#include "Console/RealmdConsole.h"
#include "Realm/RealmSnapshot.h"

#include <cstdlib>
#include <iostream>
#include <string>

using MaNGOS::Realmd::FoldAccountName;
using MaNGOS::Realmd::ApplyRealmSnapshot;
using MaNGOS::Realmd::RealmdStatus;
using MaNGOS::Realmd::FormatRelativeAge;
using MaNGOS::Realmd::FormatUptime;
using MaNGOS::Realmd::FormatRealms;
using MaNGOS::Realmd::FormatLogons;
using MaNGOS::Realmd::FormatActivity;
using MaNGOS::Realmd::FormatHeaderRight;
using MaNGOS::Realmd::FormatFailureRate;

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

void TestRelativeAgeBuckets()
{
    CHECK_EQ(FormatRelativeAge(0), "0s ago");
    CHECK_EQ(FormatRelativeAge(59), "59s ago");
    CHECK_EQ(FormatRelativeAge(60), "1m ago");
    CHECK_EQ(FormatRelativeAge(185), "3m ago");
    CHECK_EQ(FormatRelativeAge(3599), "59m ago");
    CHECK_EQ(FormatRelativeAge(7500), "2h ago");
    CHECK_EQ(FormatRelativeAge(86399), "23h ago");
    CHECK_EQ(FormatRelativeAge(200000), "2d ago");
}

void TestUptimeFormatting()
{
    CHECK_EQ(FormatUptime(0), "0d 00:00:00");
    CHECK_EQ(FormatUptime(62), "0d 00:01:02");
    CHECK_EQ(FormatUptime(273112), "3d 03:51:52");
}

void TestRealmsField()
{
    RealmdStatus status;
    status.realmsTotal = 5;
    status.realmsOnline = 4;
    status.snapshotPublished = true;
    status.snapshotAgeSeconds = 185;
    CHECK_EQ(FormatRealms(status), "4/5 \xC2\xB7 3m ago");

    RealmdStatus never;
    CHECK_EQ(FormatRealms(never), "0/0 \xC2\xB7 never");
}

void TestLogonsField()
{
    RealmdStatus status;
    status.logonsOk = 1247;
    status.logonsFailedRejected = 21;
    status.logonsFailedBadProof = 12;
    status.logonsFailedBuild = 4;
    CHECK_EQ(FormatLogons(status), "1284/37 \xC2\xB7 2.9%");

    RealmdStatus idle;
    CHECK_EQ(FormatLogons(idle), "0/0 \xC2\xB7 \xE2\x80\x94");
}

void TestActivityLineTracksPatchTransfers()
{
    // The only field in the whole status set that is not a status slot: it is
    // written to SetActivity, and it is what proves ActivePatchTransfers() is
    // wired into the gather rather than left at zero forever.
    RealmdStatus idle;
    CHECK_EQ(FormatActivity(idle), "");

    RealmdStatus one;
    one.patchTransfersActive = 1;
    CHECK_EQ(FormatActivity(one), "1 patch transfer");

    RealmdStatus many;
    many.patchTransfersActive = 7;
    CHECK_EQ(FormatActivity(many), "7 patch transfers");
}

void TestHeaderRightDatabaseStates()
{
    RealmdStatus unprobed;
    unprobed.bindIp = "0.0.0.0";
    unprobed.port = 3724;
    unprobed.uptimeSeconds = 62;
    CHECK_EQ(FormatHeaderRight(unprobed),
        "0.0.0.0:3724 \xC2\xB7 DB \xE2\x80\x94 \xC2\xB7 up 0d 00:01:02");

    RealmdStatus healthy = unprobed;
    healthy.dbProbed = true;
    healthy.dbOk = true;
    healthy.dbLatencyMs = 4;
    healthy.uptimeSeconds = 273112;
    CHECK_EQ(FormatHeaderRight(healthy),
        "0.0.0.0:3724 \xC2\xB7 DB ok 4ms \xC2\xB7 up 3d 03:51:52");

    RealmdStatus broken = unprobed;
    broken.dbProbed = true;
    CHECK_EQ(FormatHeaderRight(broken),
        "0.0.0.0:3724 \xC2\xB7 DB down \xC2\xB7 up 0d 00:01:02");

    RealmdStatus unbound;
    unbound.port = 3724;
    CHECK_EQ(FormatHeaderRight(unbound),
        "0.0.0.0:3724 \xC2\xB7 DB \xE2\x80\x94 \xC2\xB7 up 0d 00:00:00");
}

void TestAccountNameFolding()
{
    // Ordinary names are untouched.
    CHECK_EQ(FoldAccountName("ADMIN"), "ADMIN");
    CHECK_EQ(FoldAccountName(""), "");

    // Escape introducers cannot survive into a log line.
    CHECK_EQ(FoldAccountName("\x1b[2J"), "?[2J");
    CHECK_EQ(FoldAccountName("A\x7f" "B"), "A?B");

    // Wide characters are the real hazard: two terminal cells, one code
    // point. One '?' per code point, not per byte.
    CHECK_EQ(FoldAccountName("\xE4\xB8\xAD\xE6\x96\x87"), "??");
    CHECK_EQ(FoldAccountName("A\xF0\x9F\x92\x80" "B"), "A?B");
    CHECK_EQ(FoldAccountName("\xC3\x85NGSTROM"), "?NGSTROM");

    // Truncated and stray sequences must still terminate.
    CHECK_EQ(FoldAccountName("A\xC3"), "A?");
    CHECK_EQ(FoldAccountName("\x80\x80"), "??");

    // A lead byte whose promised continuation never arrives consumes ONE byte,
    // not the length its introducer claimed. C3 41 is remotely reachable and
    // must not swallow the 'A'.
    CHECK_EQ(FoldAccountName("\xC3" "A"), "?A");
    CHECK_EQ(FoldAccountName("\xE4\xB8" "Z"), "??Z");

    // Overlong two-byte encoding (C0 AF is the classic overlong '/'): C0 is
    // not a legal lead at all, so it folds as one stray byte and the AF that
    // follows folds as another.
    CHECK_EQ(FoldAccountName("\xC0\xAF"), "??");

    // Lead byte outside the encodable range (> U+10FFFF).
    CHECK_EQ(FoldAccountName("\xF5\x80"), "??");
}

void TestFailureRate()
{
    RealmdStatus status;
    status.logonsOk = 1247;
    status.logonsFailedRejected = 21;
    status.logonsFailedBadProof = 12;
    status.logonsFailedBuild = 4;
    // 37 of 1284 = 2.881%, rounded to one decimal.
    CHECK_EQ(FormatFailureRate(status), "2.9%");

    RealmdStatus perfect;
    perfect.logonsOk = 10;
    CHECK_EQ(FormatFailureRate(perfect), "0.0%");

    RealmdStatus total;
    total.logonsFailedBuild = 3;
    CHECK_EQ(FormatFailureRate(total), "100.0%");

    // No attempts at all is not a zero rate, it is no rate.
    RealmdStatus idle;
    CHECK_EQ(FormatFailureRate(idle), "\xE2\x80\x94");
}

RealmdStatus MakeFixedStatus()
{
    RealmdStatus status;
    status.bindIp = "0.0.0.0";
    status.port = 3724;
    status.patchEnabled = true;
    status.connections = 12;
    status.authWaiting = 3;
    status.peakConnections = 47;
    status.realmsTotal = 5;
    status.realmsOnline = 4;
    status.snapshotAgeSeconds = 185;
    status.snapshotPublished = true;
    status.logonsOk = 1247;
    status.logonsFailedRejected = 21;
    status.logonsFailedBadProof = 12;
    status.logonsFailedBuild = 4;
    status.patchTransfersActive = 2;
    status.dbProbed = true;
    status.dbOk = true;
    status.dbLatencyMs = 4;
    status.uptimeSeconds = 273112;
    return status;
}

void TestFixedStatusRendersEverySlot()
{
    RealmdStatus const status = MakeFixedStatus();

    CHECK_EQ(std::to_string(status.connections), "12");
    CHECK_EQ(FormatRealms(status), "4/5 \xC2\xB7 3m ago");
    CHECK_EQ(FormatLogons(status), "1284/37 \xC2\xB7 2.9%");
    CHECK_EQ(std::to_string(status.authWaiting), "3");
    CHECK_EQ(std::to_string(status.peakConnections), "47");
    CHECK_EQ(status.patchEnabled ? "on" : "off", "on");
    CHECK_EQ(FormatFailureRate(status), "2.9%");
    CHECK_EQ(FormatHeaderRight(status),
        "0.0.0.0:3724 \xC2\xB7 DB ok 4ms \xC2\xB7 up 3d 03:51:52");

    // The activity line is driven by patchTransfersActive and by nothing else.
    // If StatusSource::Gather ever stops calling ActivePatchTransfers(), this
    // is the field that silently goes blank, so assert on the value that
    // produces the string rather than on the string alone.
    CHECK(status.patchTransfersActive == 2);
    CHECK_EQ(FormatActivity(status), "2 patch transfers");

    // churn and scheduledExit are phase 4's to fill, but they are phase 2's to
    // declare. A phase that re-declares either breaks this line first.
    CHECK(status.churn.empty());
    CHECK(status.scheduledExit.empty());
}

void TestIdleStatusRendersCleanly()
{
    // What phase 2 actually produces on a fresh start with no clients: no
    // connections, no logons, no database probe. Nothing may render as an
    // empty field or a wrapped unsigned value.
    RealmdStatus const status;

    CHECK_EQ(FormatRealms(status), "0/0 \xC2\xB7 never");
    CHECK_EQ(FormatLogons(status), "0/0 \xC2\xB7 \xE2\x80\x94");
    CHECK_EQ(FormatFailureRate(status), "\xE2\x80\x94");
    CHECK_EQ(FormatActivity(status), "");
    CHECK_EQ(FormatHeaderRight(status),
        "0.0.0.0:0 \xC2\xB7 DB \xE2\x80\x94 \xC2\xB7 up 0d 00:00:00");
}

void TestHeaderRightAppendsScheduledExit()
{
    MaNGOS::Realmd::RealmdStatus status;
    status.bindIp = "0.0.0.0";
    status.port = 3724;
    status.dbProbed = true;
    status.dbOk = true;
    status.dbLatencyMs = 4;
    status.uptimeSeconds = 62;

    // Empty countdown: the header is byte-for-byte what phase 2 locked. This
    // half must PASS both before and after the change below -- it is the guard
    // that the append does not disturb the existing line.
    CHECK_EQ(MaNGOS::Realmd::FormatHeaderRight(status),
        "0.0.0.0:3724 \xC2\xB7 DB ok 4ms \xC2\xB7 up 0d 00:01:02");

    status.scheduledExit = "Restart in 12m30s";
    CHECK_EQ(MaNGOS::Realmd::FormatHeaderRight(status),
        "0.0.0.0:3724 \xC2\xB7 DB ok 4ms \xC2\xB7 up 0d 00:01:02"
        " \xC2\xB7 Restart in 12m30s");
}
}

int main()
{
    TestDefaultStatusIsZeroed();
    TestRealmSnapshotSummary();
    TestUnpublishedSnapshotHasNoAge();
    TestSnapshotAgeIgnoresBackwardsClock();

    TestRelativeAgeBuckets();
    TestUptimeFormatting();
    TestRealmsField();
    TestLogonsField();
    TestActivityLineTracksPatchTransfers();
    TestHeaderRightDatabaseStates();
    TestAccountNameFolding();
    TestFailureRate();
    TestFixedStatusRendersEverySlot();
    TestIdleStatusRendersCleanly();
    TestHeaderRightAppendsScheduledExit();
    if (failures != 0)
    {
        std::cerr << failures << " realmd console check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Realmd console checks passed\n";
    return EXIT_SUCCESS;
}
