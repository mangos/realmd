#/**
# * MaNGOS is a full featured server for World of Warcraft, supporting
# * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
# *
# * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
# *
# * This program is free software; you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation; either version 2 of the License, or
# * (at your option) any later version.
# *
# * This program is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with this program; if not, write to the Free Software
# * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
# *
# * World of Warcraft, and all World of Warcraft or Warcraft art, images,
# * and lore are copyrighted by Blizzard Entertainment, Inc.
# */

#Auth Files
file(GLOB_RECURSE SRC_GRP_AUTH Auth/*.cpp Auth/*.h)
source_group("Authentication" FILES ${SRC_GRP_AUTH})

#Realm Files
file(GLOB_RECURSE SRC_GRP_REALM Realm/*.cpp Realm/*.h)
source_group("Realmlist" FILES ${SRC_GRP_REALM})


configure_file(realmd.conf.dist.in ${CMAKE_CURRENT_BINARY_DIR}/realmd.conf.dist)

if(WIN32)
    set(PRODUCT_NAME "realmd.exe")
    set(PRODUCT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/realmd.ico)
    set(PRODUCT_DESCRIPTION "MaNGOS Authentication Daemon")
    configure_file(${CMAKE_SOURCE_DIR}/cmake/win/VersionInfo.h.in ${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.h)
endif()

add_executable(realmd
    ${SRC_GRP_AUTH}
    ${SRC_GRP_REALM}
    Main.cpp
    $<$<BOOL:${WIN32}>:
        ${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.h
        ${CMAKE_SOURCE_DIR}/cmake/win/VersionInfo.rc
    >
)

target_compile_features(realmd PUBLIC cxx_std_17)
set_target_properties(realmd PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(realmd
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(realmd
    PUBLIC
        shared
        Threads::Threads
        mangos_openssl
    PRIVATE
        mangos_openssl_strict
)

install(
    TARGETS realmd
    DESTINATION ${BIN_DIR}
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/realmd.conf.dist
    DESTINATION ${CONF_INSTALL_DIR}
)

if(WIN32 AND MSVC)
    install(
        FILES $<TARGET_PDB_FILE:realmd>
        DESTINATION ${BIN_DIR}
        OPTIONAL
        )
endif()

# Parent cores use two explicit test switches: WITH_TESTS in MangosTwo and
# BUILD_TESTING in MangosZero/MangosOne. Both default off in their respective
# parents, so realmd tests remain opt-in while working with every supported
# parent convention.
if(WITH_TESTS OR BUILD_TESTING)
    add_executable(realmd_client_locale_tests
        Tests/ClientLocaleTests.cpp
        Auth/ClientLocale.cpp
    )
    target_compile_features(realmd_client_locale_tests PUBLIC cxx_std_17)
    set_target_properties(realmd_client_locale_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(realmd_client_locale_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME realmd_client_locale_tests COMMAND realmd_client_locale_tests)

    add_executable(realmd_auth_protocol_tests
        Tests/AuthProtocolGuardTests.cpp
        Auth/AuthProtocolGuard.cpp
    )
    target_compile_features(realmd_auth_protocol_tests PUBLIC cxx_std_17)
    set_target_properties(realmd_auth_protocol_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(realmd_auth_protocol_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME realmd_auth_protocol_tests COMMAND realmd_auth_protocol_tests)

    add_executable(realmd_realm_snapshot_tests
        Tests/RealmSnapshotTests.cpp
    )
    target_compile_features(realmd_realm_snapshot_tests PUBLIC cxx_std_17)
    set_target_properties(realmd_realm_snapshot_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(realmd_realm_snapshot_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(realmd_realm_snapshot_tests PRIVATE
        shared
        Threads::Threads
    )
    add_test(NAME realmd_realm_snapshot_tests COMMAND realmd_realm_snapshot_tests)

    add_executable(realmd_client_build_policy_tests
        Tests/ClientBuildPolicyTests.cpp
        Auth/AuthResultPolicy.cpp
        Realm/ClientBuildPolicy.cpp
    )
    target_compile_features(realmd_client_build_policy_tests PUBLIC cxx_std_17)
    set_target_properties(realmd_client_build_policy_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(realmd_client_build_policy_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(realmd_client_build_policy_tests PRIVATE
        shared
    )
    add_test(NAME realmd_client_build_policy_tests COMMAND realmd_client_build_policy_tests)

    add_test(NAME realmd_auth_safety_policy
        COMMAND ${CMAKE_COMMAND}
            -DREALMD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/CheckAuthSafety.cmake
    )

    add_test(NAME realmd_srp6_width_policy
        COMMAND ${CMAKE_COMMAND}
            -DREALMD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/CheckSrp6Widths.cmake
    )

    add_executable(realmd_patch_safety_tests
        Tests/PatchSafetyTests.cpp
        Auth/PatchPolicy.cpp
        Auth/PatchArtifact.cpp
    )
    target_compile_features(realmd_patch_safety_tests PUBLIC cxx_std_17)
    set_target_properties(realmd_patch_safety_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(realmd_patch_safety_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(realmd_patch_safety_tests PRIVATE
        mangos_openssl
        mangos_openssl_strict
    )
    if(WIN32)
        # Co-locate the OpenSSL crypto runtime next to the test executable.
        #
        # OPENSSL_ROOT_DIR alone is not a usable anchor: FindOpenSSL routinely
        # resolves via OPENSSL_INCLUDE_DIR / the library paths without it ever
        # being set, in which case "${OPENSSL_ROOT_DIR}/bin" degrades to "/bin".
        # Installs also disagree on whether the DLLs sit in <prefix>/bin or
        # directly in <prefix>. So derive candidate directories from whatever the
        # OpenSSL discovery actually produced, and search all of them.
        set(_realmd_openssl_hints)
        if(OPENSSL_ROOT_DIR)
            list(APPEND _realmd_openssl_hints
                "${OPENSSL_ROOT_DIR}" "${OPENSSL_ROOT_DIR}/bin")
        endif()
        if(OPENSSL_INCLUDE_DIR)
            get_filename_component(_realmd_openssl_prefix
                "${OPENSSL_INCLUDE_DIR}" DIRECTORY)
            list(APPEND _realmd_openssl_hints
                "${_realmd_openssl_prefix}" "${_realmd_openssl_prefix}/bin")
        endif()
        foreach(_realmd_openssl_lib IN ITEMS
                ${OPENSSL_CRYPTO_LIBRARY} ${LIB_EAY_RELEASE} ${LIB_EAY})
            if(_realmd_openssl_lib)
                get_filename_component(_realmd_openssl_libdir
                    "${_realmd_openssl_lib}" DIRECTORY)
                list(APPEND _realmd_openssl_hints
                    "${_realmd_openssl_libdir}" "${_realmd_openssl_libdir}/../bin")
            endif()
        endforeach()

        # The runtime name carries the major version (libcrypto-3-x64.dll on
        # OpenSSL 3, libcrypto-4-x64.dll on OpenSSL 4), so match the family
        # rather than pinning one major.
        find_file(REALMD_TEST_OPENSSL_CRYPTO_DLL
            NAMES
                libcrypto-4-x64.dll libcrypto-4.dll
                libcrypto-3-x64.dll libcrypto-3.dll
                libcrypto-x64.dll   libcrypto.dll
            HINTS ${_realmd_openssl_hints}
        )

        # A missing test-only runtime must not break configuring the server: warn
        # and let the test fall back to whatever is on PATH.
        if(REALMD_TEST_OPENSSL_CRYPTO_DLL)
            add_custom_command(TARGET realmd_patch_safety_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${REALMD_TEST_OPENSSL_CRYPTO_DLL}"
                    "$<TARGET_FILE_DIR:realmd_patch_safety_tests>"
                COMMENT
                    "realmd_patch_safety_tests: co-locating the OpenSSL runtime"
            )
        else()
            message(WARNING
                "OpenSSL crypto runtime DLL not found; realmd_patch_safety_tests "
                "will rely on it being on PATH. Set OPENSSL_ROOT_DIR to have it "
                "copied next to the test executable.")
        endif()
    endif()
    add_test(NAME realmd_patch_safety_tests COMMAND realmd_patch_safety_tests)

    add_test(NAME realmd_patch_safety_policy
        COMMAND ${CMAKE_COMMAND}
            -DREALMD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/CheckPatchSafety.cmake
    )
endif()
