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
 * This program is distributed in the hope that it will be useful,r
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

/**
 * @file Main.cpp
 * @brief Realm daemon entry point and main loop
 *
 * This file implements the realm daemon (realmd) which handles:
 * - Client authentication and login
 * - Realm list provision
 * - Account verification
 * - Connection to world servers
 * - Database access for account data
 *
 * The realm daemon listens on a configured port for incoming client
 * connections, authenticates users against the database, and provides
 * the realm list for world server selection.
 *
 * @addtogroup realmd Realm Daemon
 * @{
 */

#include <csignal>
#include "Platform/Define.h"
#include "Common/TimeConstants.h"
#include <cstdio>
#include <cstdlib>
#include "Database/DatabaseEnv.h"
#include "Realm/RealmList.h"

#include "Config/Config.h"
#include "GitRevision.h"
#include "Log.h"
#include "Auth/PatchPolicy.h"
#include "Auth/AuthSocket.h"
#include "Auth/AuthServer.h"
#include "Auth/PatchTransferCounter.h"
#include "Console/ConsoleLifecycle.h"
#include "Console/LoginDbHealth.h"
#include "Console/RealmdConsole.h"
#include "Console/StatusSource.h"
#include "SystemConfig.h"
#include "ScheduledExit.h"
#include "Events/RealmChangeTracker.h"
#include "Events/DbHealthTracker.h"
#include "Util.h"

#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <openssl/provider.h>
#include "Auth/OpenSSLProvider.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <utility>

#ifdef WIN32
#include "ServiceWin32.h"
#include "WheatyExceptionReport.h"
char serviceName[] = "realmd";
char serviceLongName[] = "MaNGOS realmd service";
char serviceDescription[] = "Massive Network Game Object Server";
/**
 * -1 - not in service mode
 *  0 - stopped
 *  1 - running
 *  2 - paused
 */
int m_ServiceStatus = -1;
#else
#include "PosixDaemon.h"
#endif

bool StartDB();
void UnhookSignals();
void HookSignals();

#ifdef _WIN32
#include <windows.h>
#include <string>
/** Update the console title with current auth/waiting and connection counts, only if changed. */
static void UpdateConsoleTitle(uint32 authWaiting, uint32 connections)
{
    static std::string s_lastTitle;
    char title[128];
    snprintf(title, sizeof(title), "MaNGOS Realm-Daemon (%u Auth/Waiting - %u Connections)", authWaiting, connections);
    std::string newTitle(title);
    if (s_lastTitle != newTitle)
    {
        s_lastTitle = newTitle;
        SetConsoleTitleA(title);
    }
}
#endif

bool stopEvent = false;                                     ///< Setting it to true stops the server
uint8 exitCode = 0;                                         ///< Process exit code

DatabaseType LoginDatabase;                                 ///< Accessor to the realm server database

namespace
{
    uint8 const REALMD_SHUTDOWN_EXIT_CODE = 0;
    uint8 const REALMD_RESTART_EXIT_CODE = 2;

    /// Longest realmd waits at shutdown for detached patch transfers to
    /// finish, AFTER authServer.Stop() has disarmed their send channels --
    /// which is what releases one parked on backpressure. Bounded so a stalled
    /// client can never hang the shutdown, and comfortably longer than the one
    /// second a freshly accepted transfer sleeps before it can even look at
    /// flow control (Auth/PatchHandler.cpp:75). On expiry the console writer
    /// thread is deliberately left running and the process leaves without
    /// static destruction.
    std::chrono::seconds const REALMD_PATCH_DRAIN_TIMEOUT{5};

    MaNGOS::ScheduledExitSchedule s_scheduledExit;
    MaNGOS::ScheduledExitState s_scheduledExitState;

    MaNGOS::Realmd::RealmChangeTracker s_realmChanges;
    RealmSnapshotStore::SnapshotPtr s_lastRealmSnapshot;
    MaNGOS::Realmd::DbHealthTracker s_dbHealth;

    void LoadScheduledExitConfig()
    {
        s_scheduledExit = MaNGOS::ScheduledExitSchedule();
        s_scheduledExitState = MaNGOS::ScheduledExitState();

        if (!sConfig.GetBoolDefault("ScheduledExit.Enable", false))
        {
            sLog.outString("ScheduledExit: disabled");
            return;
        }

        std::string dayText = sConfig.GetStringDefault("ScheduledExit.DayOfWeek", "3");
        uint32 dayOfWeek = 0;
        if (!MaNGOS::ParseScheduledExitUInt32(dayText, dayOfWeek) || dayOfWeek > 6)
        {
            sLog.outError("ScheduledExit: invalid ScheduledExit.DayOfWeek '%s'; "
                "disabling scheduled exit", dayText.c_str());
            return;
        }

        std::string timeText = sConfig.GetStringDefault("ScheduledExit.Time", "05:00");
        uint32 hour = 0;
        uint32 minute = 0;
        if (!MaNGOS::ParseScheduledExitTime(timeText, hour, minute))
        {
            sLog.outError("ScheduledExit: invalid ScheduledExit.Time '%s'; "
                "disabling scheduled exit", timeText.c_str());
            return;
        }

        std::string modeText = sConfig.GetStringDefault("ScheduledExit.Mode", "restart");
        MaNGOS::ScheduledExitMode mode = MaNGOS::SCHEDULED_EXIT_MODE_RESTART;
        if (!MaNGOS::ParseScheduledExitMode(modeText, mode))
        {
            sLog.outError("ScheduledExit: invalid ScheduledExit.Mode '%s'; "
                "disabling scheduled exit", modeText.c_str());
            return;
        }

        s_scheduledExit.enabled = true;
        s_scheduledExit.dayOfWeek = dayOfWeek;
        s_scheduledExit.hour = hour;
        s_scheduledExit.minute = minute;
        s_scheduledExit.mode = mode;

        if (MaNGOS::MarkScheduledExitHandledIfMatching(
            s_scheduledExit, safe_localtime(time(NULL)), s_scheduledExitState))
        {
            sLog.outString("ScheduledExit: startup minute matches configured "
                "schedule; suppressing this minute to avoid restart loop");
        }

        sLog.outString("ScheduledExit: enabled day=%u time=%02u:%02u mode=%s",
            s_scheduledExit.dayOfWeek, s_scheduledExit.hour, s_scheduledExit.minute,
            MaNGOS::ScheduledExitModeToString(s_scheduledExit.mode));
    }

    void CheckScheduledExit()
    {
        if (!s_scheduledExit.enabled || stopEvent)
        {
            return;
        }

        std::tm localTime = safe_localtime(time(NULL));
        if (!MaNGOS::CheckAndMarkScheduledExit(s_scheduledExit, localTime, s_scheduledExitState))
        {
            return;
        }

        exitCode = s_scheduledExit.mode == MaNGOS::SCHEDULED_EXIT_MODE_RESTART
            ? REALMD_RESTART_EXIT_CODE : REALMD_SHUTDOWN_EXIT_CODE;
        sLog.outString("ScheduledExit: firing scheduled %s",
            MaNGOS::ScheduledExitModeToString(s_scheduledExit.mode));
        stopEvent = true;
    }

    /**
     * @brief Probe the login database, recording whether it answered and how
     *        long the call took.
     *
     * DatabaseType::Ping() (shared/Database/Database.cpp:295) returns void and
     * throws its own result away, so it cannot tell the operator whether the
     * login database is answering or how slowly. This runs realmd's own probe
     * on the same MaxPingTime cadence and records what it saw.
     *
     * The measured span starts before Query(), which takes a pooled-connection
     * lock first, so it includes any wait for a free connection and is not a
     * pure round trip. It is on the same cadence as the Ping() above it, so it
     * adds no new blocking behaviour to this loop.
     *
     * Failure is a null result, not an exception, and this function has no
     * early return: it introduces no path that could leave main with the
     * console log writer still running.
     */
    void ProbeLoginDatabase()
    {
        std::chrono::steady_clock::time_point const started =
            std::chrono::steady_clock::now();

        QueryResult* result = LoginDatabase.Query("SELECT 1");
        bool const ok = (result != nullptr);

        // Query() returns an OWNING raw pointer (Database.h:279). Without this
        // delete the probe leaks one result set every MaxPingTime minutes for
        // the lifetime of the process.
        delete result;

        uint32 const latencyMs = static_cast<uint32>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count());

        MaNGOS::Realmd::RecordLoginDbProbe(ok, latencyMs, time(NULL));
    }

    /// Diff the published realm snapshot and log only what actually changed.
    ///
    /// The tick reads the snapshot, it never refreshes it: refreshing here
    /// would win RealmRefreshGate::RunIfDue's try_to_lock essentially every
    /// time and migrate a database query plus up to three untimed
    /// getaddrinfo() calls per realm onto the thread that drives auth-deadline
    /// expiry, the MySQL ping and the scheduled-exit exact-minute match.
    ///
    /// This helper logs and does nothing else. It must never touch ConsoleUI:
    /// PublishStatus is realmd's only publisher.
    void ReportRealmChanges()
    {
        RealmSnapshotStore::SnapshotPtr snapshot = sRealmList.GetSnapshot();
        if (snapshot == s_lastRealmSnapshot)
        {
            return;
        }

        s_lastRealmSnapshot = snapshot;

        std::vector<MaNGOS::Realmd::RealmChangeEvent> events;
        if (!s_realmChanges.Observe(*snapshot, events))
        {
            return;
        }

        for (MaNGOS::Realmd::RealmChangeEvent const& event : events)
        {
            sLog.outString("%s",
                MaNGOS::Realmd::FormatRealmChangeEvent(event).c_str());
        }
    }

    /// Log login-database health only when its derived state changes.
    ///
    /// Like ReportRealmChanges, this helper logs and does nothing else. It
    /// must never touch ConsoleUI: PublishStatus is realmd's only publisher.
    void ReportDbHealth()
    {
        std::string event;
        if (s_dbHealth.Observe(
                MaNGOS::Realmd::GetLoginDbHealth(), time(NULL), event))
        {
            sLog.outString("%s", event.c_str());
        }
    }
}

/**
 * @brief Print command line usage information
 * @param prog Program name (argv[0])
 *
 * Displays usage information including available command line options.
 * Shows platform-specific options (Windows service vs POSIX daemon).
 */
void usage(const char* prog)
{
    sLog.outString("Usage: \n %s [<options>]\n"
        "    -v, --version            print version and exist\n\r"
        "    -c config_file           use config_file as configuration file\n\r"
#ifdef WIN32
        "    Running as service functions:\n\r"
        "    -s run                   run as service\n\r"
        "    -s install               install service\n\r"
        "    -s uninstall             uninstall service\n\r"
#else
        "    Running as daemon functions:\n\r"
        "    -s run                   run as daemon\n\r"
        "    -s stop                  stop daemon\n\r"
#endif
    , prog);
}

/**
 * @brief Realm daemon entry point
 * @param argc Argument count
 * @param argv Argument values
 * @return Exit code (0 for success, non-zero for errors)
 *
 * Main entry point for the realm daemon. Performs:
 * 1. Command line parsing
 * 2. Service/daemon mode handling
 * 3. Configuration loading
 * 4. Database initialization
 * 5. Network setup
 * 6. Main event loop
 * 7. Graceful shutdown
 *
 * The function runs until a shutdown signal is received or an
 * unrecoverable error occurs.
 */
extern int main(int argc, char** argv)
{
#ifdef _WIN32
    static WheatyExceptionReport exceptionReport;
    SetUnhandledExceptionFilter(WheatyExceptionReport::WheatyUnhandledExceptionFilter);
#endif

    ///- Command line parsing
    char const* cfg_file = REALMD_CONFIG_LOCATION;

    char serviceDaemonMode = '\0';

    // Minimal command-line parser. Recognised options:
    //   -c <file>            configuration file
    //   -s <mode>            service/daemon control (run / install / uninstall / stop)
    //   -v / --version       print version and exit
    // An option that takes an argument accepts it either attached ("-cFILE") or
    // as the following token ("-c FILE").
    auto takeArg = [&](int& i, char const* attached, char opt) -> char const*
    {
        if (attached && *attached)
        {
            return attached;                    // "-cFILE"
        }
        if (i + 1 < argc)
        {
            return argv[++i];                   // "-c FILE"
        }
        sLog.outError("Runtime-Error: -%c option requires an input argument", opt);
        usage(argv[0]);
        Log::WaitBeforeContinueIfNeed();
        return nullptr;
    };

    for (int i = 1; i < argc; ++i)
    {
        char const* arg = argv[i];

        // Long options.
        if (!strcmp(arg, "--version"))
        {
            printf("%s\n", GitRevision::GetProjectRevision());
            return 0;
        }

        // Short options: "-x" possibly with an attached value ("-xVALUE").
        if (arg[0] != '-' || arg[1] == '\0')
        {
            sLog.outError("Runtime-Error: bad format of commandline arguments");
            usage(argv[0]);
            Log::WaitBeforeContinueIfNeed();
            return 1;
        }

        char const opt = arg[1];
        char const* attached = arg + 2;         // "" when the value is a separate token

        switch (opt)
        {
            case 'v':
                printf("%s\n", GitRevision::GetProjectRevision());
                return 0;
            case 'c':
            {
                char const* val = takeArg(i, attached, 'c');
                if (!val)
                {
                    return 1;
                }
                cfg_file = val;
                break;
            }
            case 's':
            {
                char const* mode = takeArg(i, attached, 's');
                if (!mode)
                {
                    return 1;
                }

                if (!strcmp(mode, "run"))
                {
                    serviceDaemonMode = 'r';
                }
#ifdef WIN32
                else if (!strcmp(mode, "install"))
                {
                    serviceDaemonMode = 'i';
                }
                else if (!strcmp(mode, "uninstall"))
                {
                    serviceDaemonMode = 'u';
                }
#else
                else if (!strcmp(mode, "stop"))
                {
                    serviceDaemonMode = 's';
                }
#endif
                else
                {
                    sLog.outError("Runtime-Error: -%c unsupported argument %s", opt, mode);
                    usage(argv[0]);
                    Log::WaitBeforeContinueIfNeed();
                    return 1;
                }
                break;
            }
            default:
                sLog.outError("Runtime-Error: bad format of commandline arguments");
                usage(argv[0]);
                Log::WaitBeforeContinueIfNeed();
                return 1;
        }
    }

#ifdef WIN32                                                // windows service command need execute before config read
    switch (serviceDaemonMode)
    {
        case 'i':
            if (WinServiceInstall())
            {
                sLog.outString("Installing service");
            }
            return 1;
        case 'u':
            if (WinServiceUninstall())
            {
                sLog.outString("Uninstalling service");
            }
            return 1;
        case 'r':
            WinServiceRun();
            break;
    }
#endif

    if (!sConfig.SetSource(cfg_file))
    {
        // Try current folder as fallback if SYSCONFDIR path fails
        if (!sConfig.SetSource(REALMD_CONFIG_NAME))
        {
            sLog.outError("Could not find configuration file %s.", cfg_file);
            Log::WaitBeforeContinueIfNeed();
            return 1;
        }
        cfg_file = REALMD_CONFIG_NAME;
    }

#ifndef WIN32                                               // posix daemon commands need apply after config read
    switch (serviceDaemonMode)
    {
        case 'r':
            startDaemon();
            break;
        case 's':
            stopDaemon();
            break;
    }
#endif

    sLog.Initialize();

    sLog.outString("%s [realm-daemon]", GitRevision::GetProjectRevision());
    sLog.outString("%s", GitRevision::GetFullRevision());
    sLog.outString("<Ctrl-C> to stop.\n");
    sLog.outString("Using configuration file %s.", cfg_file);

    ///- Check the version of the configuration file
    uint32 confVersion = sConfig.GetIntDefault("ConfVersion", 0);
    if (confVersion < REALMD_CONFIG_VERSION)
    {
        sLog.outError("*****************************************************************************");
        sLog.outError(" WARNING: Your realmd.conf version indicates your conf file is out of date!");
        sLog.outError("          Please check for updates, as your current default values may cause");
        sLog.outError("          strange behavior.");
        sLog.outError("*****************************************************************************");
        Log::WaitBeforeContinueIfNeed();
    }

    LoadScheduledExitConfig();

    DETAIL_LOG("Using SSL version: %s (Library: %s)", OPENSSL_VERSION_TEXT, OpenSSL_version(OPENSSL_VERSION));

    // RAII provider management - automatically handles cleanup
    OpenSSLProviderManager providerManager;
    if (!providerManager.IsInitialized())
    {
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

    /// realmd PID file creation
    std::string pidfile = sConfig.GetStringDefault("PidFile", "");
    if (!pidfile.empty())
    {
        uint32 pid = CreatePIDFile(pidfile);
        if (!pid)
        {
            sLog.outError("Can not create PID file %s.\n", pidfile.c_str());
            Log::WaitBeforeContinueIfNeed();
            return 1;
        }

        sLog.outString("Daemon PID: %u\n", pid);
    }

    ///- Initialize the database connection
    if (!StartDB())
    {
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

    ///- Get the list of realms for the server
    sRealmList.Initialize(sConfig.GetIntDefault("RealmsStateUpdateDelay", 20));
    if (sRealmList.size() == 0)
    {
        sLog.outError("No valid realms specified.");
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

    // cleanup query
    // set expired bans to inactive
    LoginDatabase.BeginTransaction();
    LoginDatabase.Execute("UPDATE `account_banned` SET `active` = 0 WHERE `unbandate`<=UNIX_TIMESTAMP() AND `unbandate`<>`bandate`");
    LoginDatabase.Execute("DELETE FROM `ip_banned` WHERE `unbandate`<=UNIX_TIMESTAMP() AND `unbandate`<>`bandate`");
    LoginDatabase.CommitTransaction();

    ///- Launch the listening network socket
    uint16 rmport = sConfig.GetIntDefault("RealmServerPort", DEFAULT_REALMSERVER_PORT);

    // Honour the configured BindIP: empty or "0.0.0.0" listens on every local
    // interface, otherwise realmd binds only that IPv4/hostname.
    std::string bindIp = sConfig.GetStringDefault("BindIP", "0.0.0.0");
    int32 const configuredAuthTimeout =
        sConfig.GetIntDefault("AuthSessionTimeout", 30);
    uint32 authTimeoutSeconds = 30;
    if (configuredAuthTimeout < 0)
    {
        sLog.outError(
            "AuthSessionTimeout cannot be negative; using 30 seconds.");
    }
    else
    {
        authTimeoutSeconds = static_cast<uint32>(configuredAuthTimeout);
    }

    bool const patchEnabled =
        sConfig.GetBoolDefault("Patch.Enable", true);
    auto patchPolicy = PatchPolicy::Parse(
        patchEnabled,
        sConfig.GetStringDefault("Patch.ForceBuilds", ""));
    if (!patchPolicy)
    {
        sLog.outError("Invalid Patch.ForceBuilds configuration");
        return 1;
    }


    // Read before std::move(*patchPolicy) below hands the policy to
    // AuthServer: the console reports whether patch serving is on, and a
    // moved-from policy must never be consulted for it.
    bool const patchServing = patchPolicy->Enabled();

    AuthServer authServer;

    if (!authServer.Start(
            rmport,
            bindIp,
            std::chrono::seconds(authTimeoutSeconds),
            std::move(*patchPolicy)))
    {
        sLog.outError("MaNGOS realmd can not bind to port %d", rmport);
        Log::WaitBeforeContinueIfNeed();
        return 1;
    }

    ///- Catch termination signals
    HookSignals();

    ///- Take over the terminal, and give it back on every exit path
    //
    // The placement of this line is load-bearing, not stylistic. It must be
    // here, and not near sLog.Initialize() at line 406, for three independent
    // reasons:
    //
    // 1. Log::WaitBeforeContinueIfNeed bypasses the console entirely: raw
    //    printf plus std::getline(std::cin, ...). realmd calls it at lines 422,
    //    433, 445, 455, 464 and 514. Started above those, any one of the six
    //    error paths prints invisibly and then blocks on stdin underneath the
    //    alternate screen -- a daemon that looks hung.
    // 2. Six early "return 1" paths (434, 446, 456, 465, 502 and 515) sit above
    //    this line. Starting the console above them means exiting through them
    //    with the terminal taken over and never restored.
    // 3. startDaemon() forks at line 398. A thread started before a fork does
    //    not survive it.
    //
    // Constructing this does NOT necessarily start anything: for
    // Console.Style = "plain", for a service, for a daemon and for any
    // redirected run it takes no terminal and starts no thread.
    //
    // From here on ONE rule governs every exit: unless every console log
    // producer has been proven quiesced, realmd restores the terminal and
    // leaves the process from inside the guard rather than reaching static
    // destruction. The guard enforces that from its destructor, so a "return"
    // added below this line is safe -- it just will not run static destructors.
    MaNGOS::Realmd::ConsoleLifecycle console;

    ///- Handle affinity for multiple processors and process priority on Windows
#ifdef WIN32
    {
        HANDLE hProcess = GetCurrentProcess();

        uint32 Aff = sConfig.GetIntDefault("UseProcessors", 0);
        if (Aff > 0)
        {
            ULONG_PTR appAff;
            ULONG_PTR sysAff;

            if (GetProcessAffinityMask(hProcess, &appAff, &sysAff))
            {
                ULONG_PTR curAff = Aff & appAff;            // remove non accessible processors

                if (!curAff)
                {
                    sLog.outError("Processors marked in UseProcessors bitmask (hex) %x not accessible for realmd. Accessible processors bitmask (hex): %x", Aff, appAff);
                }
                else
                {
                    if (SetProcessAffinityMask(hProcess, curAff))
                    {
                        sLog.outString("Using processors (bitmask, hex): %x", curAff);
                    }
                    else
                    {
                        sLog.outError("Can't set used processors (hex): %x", curAff);
                    }
                }
            }
            sLog.outString();
        }

        bool Prio = sConfig.GetBoolDefault("ProcessPriority", false);

        if (Prio)
        {
            if (SetPriorityClass(hProcess, HIGH_PRIORITY_CLASS))
            {
                sLog.outString("realmd process priority class set to HIGH");
            }
            else
            {
                sLog.outError("Can't set realmd process priority class.");
            }
            sLog.outString();
        }
    }
#endif

    // server has started up successfully => enable async DB requests
    LoginDatabase.AllowAsyncTransactions();

    // maximum counter for next ping
    uint32 numLoops = (sConfig.GetIntDefault("MaxPingTime", 30) * (MINUTE * 1000000 / 100000));
    // The probe runs on the MaxPingTime cadence, so allow two missed rounds
    // before calling the reading stale.
    s_dbHealth.SetStaleAfter(
        time_t(sConfig.GetIntDefault("MaxPingTime", 30)) * MINUTE * 2);
    uint32 loopCounter = 0;
    uint32 logFlushCounter = 0;
    uint32 statusUpdateCounter = 0;

    // Uptime is measured from here: the listener is bound, the database is up
    // and the console is running, so this is the moment realmd began serving.
    // Not const: StatusSource::Gather is non-const by design and stays so.
    MaNGOS::Realmd::StatusSource statusSource(
        bindIp, rmport, patchServing, time(NULL));

#ifndef WIN32
    detachDaemon();
#endif
    ///- Wait for termination signal. The networking engine runs on its own
    ///- worker threads; this loop only performs periodic housekeeping.
    while (!stopEvent)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        authServer.Update();

        if ((++logFlushCounter) >= 10)
        {
            logFlushCounter = 0;
            sLog.Flush();
        }

        if ((++loopCounter) == numLoops)
        {
            loopCounter = 0;
            DETAIL_LOG("Ping MySQL to keep connection alive");
            LoginDatabase.Ping();
            ProbeLoginDatabase();
        }

        CheckScheduledExit();

        // Not a command loop. See ConsoleLifecycle::DrainInput(): the result is
        // discarded and the call exists only to stop keystrokes typed during
        // the run being replayed into the operator's shell after exit.
        console.DrainInput();
        ReportRealmChanges();
        ReportDbHealth();

        // Once a second: ten iterations of the 100 ms housekeeping loop. The
        // console writer repaints on its own roughly every 5 ms, so nothing
        // here has to call Render(). Published last in the tick so the status
        // row reflects anything logged earlier on the same iteration.
        if ((++statusUpdateCounter) >= 10)
        {
            statusUpdateCounter = 0;
            MaNGOS::Realmd::PublishStatus(statusSource.Gather(time(NULL)));
        }
#ifdef _WIN32
        static uint32 titleUpdateCounter = 0;
        if ((++titleUpdateCounter) >= 30) // ~3 seconds at 100ms reactor interval
        {
            titleUpdateCounter = 0;
            UpdateConsoleTitle(AuthSocket::GetAuthWaitingCount(), AuthSocket::GetConnectionCount());
        }
#endif
#ifdef WIN32
        if (m_ServiceStatus == 0)
        {
            stopEvent = true;
        }
        while (m_ServiceStatus == 2)
        {
            Sleep(1000);
        }
#endif
    }

    ///- The proof that every console log producer has quiesced. It DEFAULTS to
    ///- false and is raised only by a drain that actually succeeded, so no
    ///- path out of the block below can leave it optimistically true.
    bool transfersDrained = false;

    ///- The whole shutdown is enclosed, because the emergency route has to be
    ///- non-throwing END TO END and everything here can throw: Stop() joins
    ///- threads, the cancellation reaches transport callbacks, the drain waits
    ///- on a condition variable, the diagnostic formats a string,
    ///- HaltDelayThread() talks to MySQL and Flush() walks the log buffers. An
    ///- exception escaping any of them would unwind past the guard's Finish()
    ///- with the producers unproven -- and on MSVC an exception that escapes
    ///- main terminates WITHOUT unwinding, so the guard's destructor is not a
    ///- substitute for this catch.
    try
    {
        ///- Stop accepting connections and join the network worker threads.
        ///- This is also what releases a patch transfer parked on
        ///- backpressure: the transport's stop() tears every connection down
        ///- and the tear-down disarms the send channel, whose out.close()
        ///- wakes the producer so its flow->awaitWritable() returns false and
        ///- the closure returns instead of reaching its error log.
        authServer.Stop();

        ///- Best-effort cleanup on top of that stop, for a transfer that is
        ///- already disarmed by Stop(), so this call has no transport effect
        ///- on any of the three backends. It is NOT what unblocks a parked
        ///- producer: a
        ///- net::Closer records graceful-close intent, and the connection is
        ///- closed only once its outbound queue has drained -- which never
        ///- happens against a peer that has stopped reading.
        ///-
        ///- It must stay BELOW authServer.Stop(). IOCP's requestClose()
        ///- dereferences a raw connection context after releasing the channel
        ///- mutex, so a concurrent disarm can retire that context underneath
        ///- it; this call is safe only because every channel is already
        ///- disarmed by the time it runs.
        MaNGOS::Realmd::CancelPatchTransfers();

        ///- Then wait for the detached streaming closures to be gone. They are
        ///- console log producers that nothing joins, and the wait is bounded
        ///- so a stalled client can never hang the shutdown.
        transfersDrained =
            MaNGOS::Realmd::DrainPatchTransfers(REALMD_PATCH_DRAIN_TIMEOUT);

        if (!transfersDrained)
        {
            ///- Diagnostic only, and deliberately best effort: this must not
            ///- become the thing that throws on the way to the emergency exit.
            try
            {
                sLog.outError("Shutdown: %u patch transfer(s) still running "
                    "after cancellation; leaving without static destruction so "
                    "nothing is destroyed under a live log producer",
                    MaNGOS::Realmd::ActivePatchTransfers());
            }
            catch (...)
            {
            }

            ///- Straight out from here, without touching anything else. DOES
            ///- NOT RETURN: the guard restores the terminal and terminates the
            ///- process with exitCode, running no static destructors, because
            ///- returning would close the log files and destroy ConsoleUI
            ///- underneath a producer that is still logging. That is so
            ///- whether or not a console was ever started -- the writer is not
            ///- the only producer. The SQL delay thread is halted inside that
            ///- path, for the same flush the line below performs.
            console.Finish(false, exitCode);
        }

        ///- Wait for the delay thread to exit. It joins the last console
        ///- producer AND flushes the login-DB writes queued through PExecute.
        LoginDatabase.HaltDelayThread();

        sLog.outString("Halting process...");
        sLog.Flush();

        ///- Finalise the console: stop the writer, then give the terminal back.
        ///- Reached only with transfersDrained true, so this is the ordinary
        ///- path and it returns.
        console.Finish(transfersDrained, exitCode);
    }
    catch (...)
    {
        ///- Nothing above got as far as proving the producers quiesced, so the
        ///- only safe answer is the same emergency route. DOES NOT RETURN.
        console.Finish(false, exitCode);
    }

    ///- Remove signal handling only now. The handlers stay installed THROUGH
    ///- the terminal restoration above, so a Ctrl-C during teardown cannot take
    ///- the default action with the alternate screen still up and, on POSIX,
    ///- echo and line editing still switched off. Unhooking earlier -- which is
    ///- what this file used to do -- reopens exactly that window.
    UnhookSignals();

    return exitCode;

    // ~ConsoleLifecycle still runs after this return. It is a no-op now:
    // Finish() completed, so it simply confirms the terminal is restored. It
    // remains the backstop for any return added above.
}

/// Handle termination signals

/** Put the global variable stopEvent to 'true' if a termination signal is caught **/
void OnSignal(int s)
{
    switch (s)
    {
        case SIGINT:
        case SIGTERM:
            stopEvent = true;
            break;
#ifdef _WIN32
        case SIGBREAK:
            stopEvent = true;
            break;
#endif
    }

    signal(s, OnSignal);
}

/// Initialize connection to the database
bool StartDB()
{
    std::string dbstring = sConfig.GetStringDefault("LoginDatabaseInfo", "");
    if (dbstring.empty())
    {
        sLog.outError("Database not specified");
        return false;
    }

    sLog.outString("Login Database total connections: %i", 1 + 1);

    if (!LoginDatabase.Initialize(dbstring.c_str()))
    {
        sLog.outError("Can not connect to database");
        return false;
    }

    if (!LoginDatabase.CheckDatabaseVersion(DATABASE_REALMD))
    {
        ///- Wait for already started DB delay threads to end
        LoginDatabase.HaltDelayThread();
        return false;
    }

    return true;
}

/// Define hook 'OnSignal' for all termination signals
void HookSignals()
{
    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
#ifdef _WIN32
    signal(SIGBREAK, OnSignal);
#endif
}

/// Unhook the signals before leaving
void UnhookSignals()
{
    signal(SIGINT, 0);
    signal(SIGTERM, 0);
#ifdef _WIN32
    signal(SIGBREAK, 0);
#endif
}

/// @}
