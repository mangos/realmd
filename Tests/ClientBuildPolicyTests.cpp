#include "Realm/ClientBuildPolicy.h"

#include <cstdlib>
#include <iostream>

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

struct ExpectedBuild
{
    uint32 build;
    RealmVersion version;
    int major;
    int minor;
    int bugfix;
    int hotfix;
};

void TestSupportedBuildsResolveExactly()
{
    ExpectedBuild const supported[] =
    {
        {5875, REALM_VERSION_VANILLA, 1, 12, 1, ' '},
        {6005, REALM_VERSION_VANILLA, 1, 12, 2, ' '},
        {6141, REALM_VERSION_VANILLA, 1, 12, 3, ' '},
        {8606, REALM_VERSION_TBC, 2, 4, 3, ' '},
        {12340, REALM_VERSION_WOTLK, 3, 3, 5, 'a'},
        {15595, REALM_VERSION_CATA, 4, 3, 4, ' '},
        {18273, REALM_VERSION_MOP, 5, 4, 8, ' '},
        {18414, REALM_VERSION_MOP, 5, 4, 8, ' '},
        {21742, REALM_VERSION_WOD, 6, 2, 4, ' '},
        {26972, REALM_VERSION_LEGION, 7, 3, 5, ' '},
        {35662, REALM_VERSION_BFA, 8, 3, 7, ' '},
        {40000, REALM_VERSION_SHADOWLANDS, 9, 0, 0, ' '},
    };

    for (ExpectedBuild const& expected : supported)
    {
        ClientBuildPolicy const* policy =
            FindClientBuildPolicy(expected.build);
        CHECK(policy != nullptr);
        if (!policy)
        {
            continue;
        }

        CHECK(policy->realmVersion == expected.version);
        CHECK(policy->buildInfo.build == static_cast<int>(expected.build));
        CHECK(policy->buildInfo.major_version == expected.major);
        CHECK(policy->buildInfo.minor_version == expected.minor);
        CHECK(policy->buildInfo.bugfix_version == expected.bugfix);
        CHECK(policy->buildInfo.hotfix_version == expected.hotfix);
        CHECK(FindBuildInfo(static_cast<uint16>(expected.build)) ==
              &policy->buildInfo);
    }
}

void TestUnknownBuildsAreRejected()
{
    uint32 const unsupported[] = {0, 1, 39999, 40001, 65535};
    for (uint32 build : unsupported)
    {
        CHECK(FindClientBuildPolicy(build) == nullptr);
        CHECK(FindBuildInfo(static_cast<uint16>(build)) == nullptr);
    }
}
}

int main()
{
    TestSupportedBuildsResolveExactly();
    TestUnknownBuildsAreRejected();

    if (failures != 0)
    {
        std::cerr << failures << " client build policy check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Client build policy checks passed\n";
    return EXIT_SUCCESS;
}
