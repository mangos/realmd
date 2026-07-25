/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef MANGOS_H_REALMSNAPSHOT
#define MANGOS_H_REALMSNAPSHOT

#include "Common/ServerDefines.h"
#include "Platform/Define.h"

#include <array>
#include <atomic>
#include <ctime>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>

struct RealmAddress
{
    uint32 ip = 0;
    uint16 port = 0;
    bool loopback = false;
};

struct RealmBuildInfo
{
    int build;
    int major_version;
    int minor_version;
    int bugfix_version;
    int hotfix_version;
};

enum RealmVersion
{
    REALM_VERSION_VANILLA     = 0,
    REALM_VERSION_TBC         = 1,
    REALM_VERSION_WOTLK       = 2,
    REALM_VERSION_CATA        = 3,
    REALM_VERSION_MOP         = 4,
    REALM_VERSION_WOD         = 5,
    REALM_VERSION_LEGION      = 6,
    REALM_VERSION_BFA         = 7,
    REALM_VERSION_SHADOWLANDS = 8,
    REALM_VERSION_COUNT       = 9
};

RealmBuildInfo const* FindBuildInfo(uint16 build);

using RealmBuilds = std::set<uint32>;

struct Realm
{
    std::string name;
    RealmAddress ExternalAddress;
    RealmAddress LocalAddress;
    RealmAddress LocalSubnetMask;
    uint8 icon;
    RealmFlags realmflags;
    uint8 timezone;
    uint32 m_ID;
    AccountTypes allowedSecurityLevel;
    float populationLevel;
    RealmBuilds realmbuilds;
    RealmBuildInfo realmBuildInfo;
};

using RealmMap = std::map<std::string, Realm>;
using RealmStlList = std::list<Realm const*>;

struct RealmSnapshot
{
    RealmMap realms;
    std::array<RealmStlList, REALM_VERSION_COUNT> realmsByVersion;
};

class RealmSnapshotStore
{
public:
    using SnapshotPtr = std::shared_ptr<RealmSnapshot const>;

    RealmSnapshotStore()
        : m_snapshot(std::make_shared<RealmSnapshot>())
    {
    }

    SnapshotPtr Load() const
    {
        return std::atomic_load_explicit(
            &m_snapshot, std::memory_order_acquire);
    }

    void Publish(std::shared_ptr<RealmSnapshot> snapshot)
    {
        SnapshotPtr immutable = std::move(snapshot);
        std::atomic_store_explicit(
            &m_snapshot, std::move(immutable), std::memory_order_release);
    }

private:
    SnapshotPtr m_snapshot;
};

class RealmListView
{
public:
    using const_iterator = RealmStlList::const_iterator;

    RealmListView(
        RealmSnapshotStore::SnapshotPtr snapshot,
        RealmVersion version)
        : m_snapshot(std::move(snapshot)),
          m_realms(&m_snapshot->realmsByVersion[
              static_cast<std::size_t>(version)])
    {
    }

    const_iterator begin() const { return m_realms->begin(); }
    const_iterator end() const { return m_realms->end(); }
    std::size_t size() const { return m_realms->size(); }
    bool empty() const { return m_realms->empty(); }

private:
    RealmSnapshotStore::SnapshotPtr m_snapshot;
    RealmStlList const* m_realms;
};

class RealmRefreshGate
{
public:
    RealmRefreshGate(uint32 interval = 0, time_t nextUpdate = 0)
        : m_interval(interval), m_nextUpdate(nextUpdate)
    {
    }

    void Reset(uint32 interval, time_t nextUpdate)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_interval = interval;
        m_nextUpdate = nextUpdate;
    }

    template<class Refresh>
    bool RunIfDue(time_t now, Refresh&& refresh)
    {
        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock || !m_interval || m_nextUpdate > now)
        {
            return false;
        }

        m_nextUpdate = now + m_interval;
        std::forward<Refresh>(refresh)();
        return true;
    }

private:
    std::mutex m_mutex;
    uint32 m_interval;
    time_t m_nextUpdate;
};

#endif
