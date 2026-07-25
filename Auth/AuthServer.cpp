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
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

/** \file
 \ingroup realmd
 */

#include <cstdint>
#include <string>
#include "AuthServer.h"
#include "AuthSocket.h"

#include "net/Server.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

/// Holds the value-type networking engine, kept in the .cpp so its platform
/// headers stay out of AuthServer.h (and therefore out of Main.cpp).
struct AuthServer::Impl
{
    net::Server server;
    std::mutex socketsMutex;
    std::vector<std::weak_ptr<AuthSocket>> sockets;
    std::chrono::seconds authTimeout{30};
    PatchPolicy patchPolicy;
};

AuthServer::AuthServer()
    : m_impl(new Impl())
{
}

AuthServer::~AuthServer()
{
    Stop();
}

bool AuthServer::Start(
    uint16_t port,
    const std::string& bindIp,
    std::chrono::seconds authTimeout,
    PatchPolicy patchPolicy)
{
    m_impl->authTimeout = authTimeout;
    m_impl->patchPolicy = std::move(patchPolicy);
    Impl* const impl = m_impl.get();

    return m_impl->server.start(port, [impl]() -> std::shared_ptr<net::ISession>
    {
        std::shared_ptr<AuthSocket> socket =
            std::make_shared<AuthSocket>(
                impl->authTimeout, impl->patchPolicy);
        {
            std::lock_guard<std::mutex> lock(impl->socketsMutex);
            impl->sockets.emplace_back(socket);
        }
        return socket;
    }, bindIp);
}

void AuthServer::Update()
{
    std::vector<std::shared_ptr<AuthSocket>> sockets;
    {
        std::lock_guard<std::mutex> lock(m_impl->socketsMutex);
        auto socket = m_impl->sockets.begin();
        while (socket != m_impl->sockets.end())
        {
            if (std::shared_ptr<AuthSocket> active = socket->lock())
            {
                sockets.push_back(std::move(active));
                ++socket;
            }
            else
            {
                socket = m_impl->sockets.erase(socket);
            }
        }
    }

    MaNGOS::Auth::Deadline::Clock::time_point const now =
        MaNGOS::Auth::Deadline::Clock::now();
    for (std::shared_ptr<AuthSocket> const& socket : sockets)
    {
        socket->ExpireAuthentication(now);
    }
}

void AuthServer::Stop()
{
    if (m_impl)
    {
        m_impl->server.stop();
        std::lock_guard<std::mutex> lock(m_impl->socketsMutex);
        m_impl->sockets.clear();
    }
}
