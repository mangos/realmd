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

#include "ConsoleLifecycle.h"

#include "Config/Config.h"
#include "Console/ConsoleUI.h"
#include "Console/Terminal.h"
#include "Database/DatabaseEnv.h"
#include "GitRevision.h"
#include "Log.h"
#include "Platform/Define.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace MaNGOS::Realmd
{

// Defined first, deliberately: it is the single implementation of the
// invariant, and Tests/CheckConsoleLifecycle.cmake reads its body by position.
void ConsoleLifecycle::LeaveNow()
{
    // Reached whenever nothing has proven the console log producers joined.
    // Returning to the caller is not an option: static destruction closes the
    // log files (Log.h:199-202) while a detached patch-transfer closure can
    // still be calling sLog.outError (Auth/PatchHandler.cpp:107), and destroys
    // the ConsoleUI function-local static (ConsoleUI.cpp:272-276) underneath a
    // writer thread that repaints every 5 ms. So the process leaves here.
    //
    // Everything before the restoration is best effort. This runs from
    // exception handlers, so nothing here may throw its way past the point
    // where the terminal is given back.
    //
    // TWO try blocks, not one. Sharing a single try was a real defect: sLog
    // .Flush() walks the log's own buffers and can throw, and one throw then
    // skipped the database halt entirely, discarding every queued login-DB
    // write. Each step is independent, so each gets its own guard and a failure
    // in either still leaves the other attempted. Do not merge them.
    try
    {
        sLog.Flush();
    }
    catch (...)
    {
        // Swallowed on purpose: a failed log flush may not stop the database
        // flush below, nor an operator getting a usable shell back.
    }

    try
    {
        // Run for its FLUSH, not for quiescence. Main.cpp calls
        // AllowAsyncTransactions() at line 573, so last_ip, last_login and
        // failed_logins updates are queued through PExecute, and the exit below
        // runs no destructors and no atexit handlers -- without this, every
        // queued write is discarded and its MySQL connection abandoned
        // mid-flight. Idempotent (Database.cpp:230-235), so the normal shutdown
        // having already called it costs nothing. It is safe here precisely
        // because this path never calls Log::StopConsoleThread(): joining the
        // SQL delay thread removes a console producer and deletes nothing.
        LoginDatabase.HaltDelayThread();
    }
    catch (...)
    {
        // Swallowed on purpose, for the same reason.
    }

    RestoreTerminal();

    // The exit below flushes nothing. ConsoleUI::Stop() flushes stdout itself
    // (ConsoleUI.cpp:322), but the log tail may have reached other streams.
    fflush(NULL);

    std::_Exit(m_exitCode);
}

ConsoleLifecycle::ConsoleLifecycle()
    : m_writerRunning(false),
      m_uiActive(false),
      m_finished(false),
      m_exitCode(EXIT_FAILURE)
{
    try
    {
        // Read first, because the entire decision is made before anything is
        // started. "auto" and "fancy" both ask for the display; "plain" asks
        // for the line-oriented logger realmd has always had.
        std::string const style =
            sConfig.GetStringDefault("Console.Style", "auto");

        // The gate, and it is a real call rather than a claim in a comment.
        // IsInteractive() (Terminal.h:81) is the same probe Terminal::Enter()
        // performs (Terminal.cpp:219 on Windows, :351 on POSIX), so it cannot
        // disagree with the display's own decision in any ordinary case.
        //
        // Nothing above this line has started a thread or touched the terminal,
        // so returning here leaves realmd byte-identical to the build before
        // this change: no writer, m_writerRunning still false, and main()
        // returns normally -- which is what lets ServiceMain complete its
        // SERVICE_STOP_PENDING / SERVICE_STOPPED handshake on return
        // (shared/Win/ServiceWin32.cpp:286-297). mangosd starts its writer
        // unconditionally; realmd deliberately does not.
        //
        // This is a correctness-of-behaviour rule, not a safety rule. Safety is
        // Finish()'s producer test, which applies to this configuration too.
        if (style == "plain" || !MaNGOS::Console::Terminal::IsInteractive())
        {
            return;
        }

        // Raised immediately BEFORE the call, never after. MaNGOS::Thread's
        // constructor auto-starts run() (Log.cpp:484-492), so if
        // StartConsoleThread() throws part way there is no way from here to
        // tell whether a writer thread exists. The invariant is about what can
        // be PROVEN, so the ambiguous case counts as running.
        m_writerRunning = true;

        // Order is mandatory. ConsoleLogWriter is the sole caller of
        // ConsoleUI::Render() (ConsoleLogWriter.cpp:90, :101), so a display
        // started before the writer paints an empty frame while
        // Log::ConsoleEmit's synchronous fallback writes raw log text over the
        // top of it. Idempotent, which is load-bearing rather than defensive:
        // WinServiceRun() at Main.cpp:377 re-enters main().
        sLog.StartConsoleThread();

        if (!MaNGOS::Console::ConsoleUI::Instance().Start(
                "MaNGOS realmd", GitRevision::GetProjectRevision()))
        {
            // Enter() can still decline where IsInteractive() agreed -- an old
            // console host with no VT processing to enable. The writer is then
            // running with nothing to render, which the invariant already
            // covers; the window is narrowed, not opened.
            return;
        }

        m_uiActive = true;

        MaNGOS::Console::ConsoleUI& ui = MaNGOS::Console::ConsoleUI::Instance();

        // realmd has no command loop, so it must not show the default
        // "mangos> " prompt it would never read. SetHint() does not replace the
        // prompt, and the hint deliberately does not copy mangosd's scroll-key
        // text: realmd drives no such keys and advertising them would be false.
        ui.SetPrompt("");
        ui.SetHint("observe only \xC2\xB7 <Ctrl-C> to stop");

        // SetScrollback() takes unsigned and clamps upwards only, to 100
        // (ConsoleUI.cpp:427-436), so a negative configured value would
        // sign-convert into billions of retained lines. Zero and below are
        // rejected here and the compiled-in default used instead; a positive
        // value below 100 is left to SetScrollback to raise.
        // realmd.conf.dist.in documents exactly that pair of rules.
        int32 const fallback = int32(MaNGOS::Console::DEFAULT_SCROLLBACK);
        int32 const configured =
            sConfig.GetIntDefault("Console.Scrollback", fallback);
        ui.SetScrollback(
            configured > 0 ? unsigned(configured) : unsigned(fallback));
    }
    catch (...)
    {
        // Every call above can throw: ConsoleUI::Start() calls Terminal::Enter()
        // (ConsoleUI.cpp:286) before assigning its own m_title/m_subtitle
        // (:291-292), and the config reads, SetPrompt, SetHint and SetScrollback
        // all build or copy std::strings. Rethrowing is the defect this class
        // was rewritten to remove: the object was never constructed, so
        // ~ConsoleLifecycle would never run, and the stack would unwind out of
        // main with the producers unproven and the terminal on the alternate
        // screen. Take the invariant's path instead.
        //
        // Nothing is lost by not propagating: an exception escaping here
        // previously reached no handler in main and ended in std::terminate,
        // with the terminal wrecked. A throw from the Console.Style read, before
        // anything was started, takes this path too -- deliberately, because the
        // alternative on MSVC is the same terminate with the queued login-DB
        // writes discarded.
        LeaveNow();
    }
}

ConsoleLifecycle::~ConsoleLifecycle()
{
    // The backstop for every path that never reached a completed Finish(): an
    // early return added below the guard, or an exception unwinding through an
    // intervening catch. Reaching here unfinished means nobody ever proved the
    // producers were joined -- and the writer is not the only one of those, so
    // this tests the PROOF, not the writer.
    //
    // NOT covered, and deliberately not claimed: an exception that escapes main
    // entirely. MSVC calls std::terminate WITHOUT unwinding the stack, so this
    // destructor never runs. That outcome is still memory-safe -- abort runs no
    // static destructors and no atexit handlers, so nothing a producer uses is
    // destroyed -- but the terminal is left on the alternate screen, and on
    // POSIX with ECHO/ICANON cleared. Recorded rather than papered over.
    if (!m_finished)
    {
        LeaveNow();
    }

    RestoreTerminal();
}

void ConsoleLifecycle::Finish(bool producersQuiesced, int exitCode)
{
    // Recorded first so the emergency path below leaves with the code the
    // caller intended -- 2 for a scheduled restart, which a wrapper script
    // reads as "start me again".
    m_exitCode = exitCode;

    // On the producers ALONE. Not "m_writerRunning && ...": the writer is not
    // the only console log producer. A detached patch-transfer closure calls
    // sLog.outError (Auth/PatchHandler.cpp:107) and ~Log() closes the log files
    // during static destruction (Log.h:199-202), so an unproven producer is
    // fatal whether or not a writer was ever started -- including for
    // Console.Style = "plain" and for a Windows service.
    if (!producersQuiesced)
    {
        LeaveNow();
    }

    if (m_writerRunning)
    {
        // Proven quiesced: the final drain, the render of that drain and the
        // join all happen inside here, on the writer thread, before it is
        // deleted.
        sLog.StopConsoleThread();
        m_writerRunning = false;
    }

    // Last, and only now: no repaint can race Terminal::Leave() once the sole
    // caller of Render() has been joined.
    RestoreTerminal();

    // Set only on the path that returns. The destructor reads it to decide
    // whether anything ever proved quiescence.
    m_finished = true;
}

void ConsoleLifecycle::RestoreTerminal()
{
    // Cleared first so this is idempotent: LeaveNow(), Finish() and the
    // destructor can each reach it.
    bool const wasActive = m_uiActive;
    m_uiActive = false;

    if (wasActive)
    {
        try
        {
            // Best effort ONLY. Stop() assigns a 25-line tail out of the log
            // deque (ConsoleUI.cpp:310) BEFORE it calls Terminal::Leave()
            // (:313), so a bad_alloc escapes it with the terminal still
            // captured -- and on the emergency path an escaping exception is
            // std::terminate with the alternate screen up. The tail replay is
            // a nicety; the terminal is not.
            MaNGOS::Console::ConsoleUI::Instance().Stop();
        }
        catch (...)
        {
        }
    }

    // Unconditional, and the real guarantee. It covers three cases the branch
    // above cannot: Stop() threw before reaching Leave(); ConsoleUI::Start()
    // threw between Terminal::Enter() (:286) and m_active = true (:293), which
    // captures the terminal while leaving m_uiActive false here; and Enter()
    // never ran at all. Leave() early-returns on !s_active (Terminal.cpp:259,
    // :379), allocates nothing and is documented safe after a failed or absent
    // Enter() (Terminal.h:96), so the redundant call on the ordinary path costs
    // one branch.
    try
    {
        MaNGOS::Console::Terminal::Leave();
    }
    catch (...)
    {
    }
}

void ConsoleLifecycle::DrainInput()
{
    if (!m_uiActive)
    {
        return;
    }

    // Deliberately odd-looking. The returned line is thrown away: realmd reads
    // no commands, and this call exists only so the Windows console input
    // buffer is consumed rather than replayed into the shell after exit. Do not
    // "tidy" it into a command handler.
    std::string discarded;
    MaNGOS::Console::ConsoleUI::Instance().PollInput(discarded);
}

}
