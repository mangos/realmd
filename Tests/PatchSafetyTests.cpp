#include "Auth/PatchArtifact.h"
#include "Auth/PatchPolicy.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

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

void TestDefaultPolicyOnlyPatchesUnsupportedBuilds()
{
    auto policy = PatchPolicy::Parse(true, "");
    CHECK(policy.has_value());
    CHECK(policy->ShouldPatch(5000, false));
    CHECK(!policy->ShouldPatch(5875, true));
}

void TestForcedPolicyMatchesExactAcceptedBuilds()
{
    auto policy = PatchPolicy::Parse(true, "5875 6005");
    CHECK(policy.has_value());
    CHECK(policy->ShouldPatch(5875, true));
    CHECK(policy->ShouldPatch(6005, true));
    CHECK(!policy->ShouldPatch(6141, true));
}

void TestDisabledPolicyNeverPatches()
{
    auto policy = PatchPolicy::Parse(false, "5875");
    CHECK(policy.has_value());
    CHECK(!policy->ShouldPatch(5000, false));
    CHECK(!policy->ShouldPatch(5875, true));
}

void TestInvalidBuildTokensAreRejected()
{
    CHECK(!PatchPolicy::Parse(true, "0").has_value());
    CHECK(!PatchPolicy::Parse(true, "65536").has_value());
    CHECK(!PatchPolicy::Parse(true, "5875x").has_value());
}

std::string Hex(
    std::array<std::uint8_t, MD5_DIGEST_LENGTH> const& digest)
{
    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (std::uint8_t byte : digest)
    {
        hex << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return hex.str();
}

std::string ReadAll(PatchArtifact& artifact)
{
    std::string contents;
    std::array<std::uint8_t, 8> buffer{};
    for (;;)
    {
        std::streamsize const read =
            artifact.Read(buffer.data(), buffer.size());
        if (read <= 0)
        {
            break;
        }
        contents.append(
            reinterpret_cast<char const*>(buffer.data()),
            static_cast<std::size_t>(read));
    }
    return contents;
}

std::filesystem::path UniqueTempPath(char const* suffix)
{
    auto const nonce =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("realmd-patch-artifact-" + std::to_string(nonce) + suffix);
}

void TestArtifactRetainsSizeDigestAndBytes()
{
    std::filesystem::path const path = UniqueTempPath(".mpq");
    {
        std::ofstream output(path, std::ios::binary);
        output << "abcdef";
    }

    auto artifact = PatchArtifact::Open(path.string());
    CHECK(artifact != nullptr);
    if (artifact)
    {
        CHECK(artifact->size() == 6);
        CHECK(Hex(artifact->digest()) ==
            "e80b5017098950fc58aad83c8c14978e");

        CHECK(artifact->Seek(0));
        CHECK(ReadAll(*artifact) == "abcdef");
        CHECK(artifact->Seek(3));
        CHECK(ReadAll(*artifact) == "def");
        CHECK(artifact->Seek(artifact->size()));
        CHECK(ReadAll(*artifact).empty());
        CHECK(!artifact->Seek(artifact->size() + 1));
    }

    artifact.reset();
    std::error_code cleanupError;
    bool const removed = std::filesystem::remove(path, cleanupError);
    CHECK(removed && !cleanupError);
}

void TestArtifactRejectsInvalidSources()
{
    std::filesystem::path const missing = UniqueTempPath("-missing.mpq");
    CHECK(PatchArtifact::Open(missing.string()) == nullptr);
    CHECK(PatchArtifact::Open(
        std::filesystem::temp_directory_path().string()) == nullptr);

    std::filesystem::path const empty = UniqueTempPath("-empty.mpq");
    {
        std::ofstream output(empty, std::ios::binary);
    }
    CHECK(PatchArtifact::Open(empty.string()) == nullptr);
    std::error_code cleanupError;
    bool const removed = std::filesystem::remove(empty, cleanupError);
    CHECK(removed && !cleanupError);
}
}

int main()
{
    TestDefaultPolicyOnlyPatchesUnsupportedBuilds();
    TestForcedPolicyMatchesExactAcceptedBuilds();
    TestDisabledPolicyNeverPatches();
    TestInvalidBuildTokensAreRejected();
    TestArtifactRetainsSizeDigestAndBytes();
    TestArtifactRejectsInvalidSources();

    if (failures != 0)
    {
        std::cerr << failures << " patch safety check(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Patch safety checks passed\n";
    return EXIT_SUCCESS;
}
