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

if(BUILD_TESTING)
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

    add_test(NAME realmd_auth_safety_policy
        COMMAND ${CMAKE_COMMAND}
            -DREALMD_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/CheckAuthSafety.cmake
    )
endif()
