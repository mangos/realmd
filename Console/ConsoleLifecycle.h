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
/// \addtogroup realmd
/// @{
/// \file

#ifndef MANGOS_H_CONSOLELIFECYCLE
#define MANGOS_H_CONSOLELIFECYCLE

namespace MaNGOS::Realmd
{
    /**
     * @brief Scope guard owning realmd's console for the daemon's lifetime.
     *
     * Construction starts NOTHING unless a display will actually run: it reads
     * Console.Style, asks MaNGOS::Console::Terminal::IsInteractive(), and
     * returns having started no thread and touched no terminal for "plain" and
     * for any non-interactive run. Otherwise it starts the off-thread console
     * writer and then the full-screen display, in that order. realmd
     * deliberately differs from mangosd here, which starts its writer
     * unconditionally.
     *
     * ONE invariant governs every exit path:
     *
     *   If realmd cannot PROVE that every console log producer has quiesced, it
     *   restores the terminal and leaves the process without running static
     *   destructors. It never reaches static destruction.
     *
     * Note what that does not say: it does not mention the writer. The writer
     * is not the only console log producer. A detached patch-transfer closure
     * calls sLog.outError (Auth/PatchHandler.cpp:107) from a thread nothing
     * joins, and ~Log() closes every log file during static destruction
     * (Log.h:199-202). Gating the emergency exit on the writer therefore
     * exempts exactly the configurations that never start one -- "plain", a
     * Windows service, a redirected run -- which is the defect this class was
     * rewritten to remove.
     *
     * The writer, where one exists, makes it worse rather than different:
     * ConsoleLogWriter::run() calls MaNGOS::Console::ConsoleUI::Instance()
     * .Render() and writes stdout every 5 ms (ConsoleLogWriter.cpp:76-102);
     * ConsoleUI is a function-local static with no destructor of its own
     * (ConsoleUI.cpp:272-276) and is destroyed BEFORE Log, so static
     * destruction destroys the object that thread is rendering into.
     *
     * Exactly one instance may exist at a time; both components it drives are
     * process-wide singletons.
     */
    class ConsoleLifecycle
    {
        public:
            ConsoleLifecycle();
            ~ConsoleLifecycle();

            ConsoleLifecycle(ConsoleLifecycle const&) = delete;
            ConsoleLifecycle& operator=(ConsoleLifecycle const&) = delete;

            /// @return true when the full-screen display took the terminal.
            bool Active() const { return m_uiActive; }

            /**
             * @brief Finalise the console.
             *
             * Returns ONLY when producersQuiesced is true. When it is
             * false this restores the terminal and terminates the
             * process, so it does not return on that path. It is
             * deliberately NOT marked [[noreturn]] -- that attribute
             * belongs on LeaveNow(), which never returns.
             *
             * Call once, from main, AFTER every console producer has been
             * dealt with and BEFORE UnhookSignals(). Both halves of that
             * sentence are requirements:
             *
             * - Log::StopConsoleThread() frees the writer body while a producer
             *   already past the m_consoleAsync check in Log::ConsoleEmit can
             *   still be holding it, and static destruction closes the log
             *   files outright, so neither may happen until the caller can
             *   assert that the network workers, the SQL delay thread and every
             *   detached patch transfer are done. That assertion is the
             *   producersQuiesced argument.
             * - The signal handlers must still be installed while the terminal
             *   is restored, or a Ctrl-C during teardown takes the default
             *   action with the alternate screen still up and, on POSIX, the
             *   terminal still in raw mode.
             *
             * @param producersQuiesced true only when every console producer is
             *        joined. FALSE MEANS THIS CALL DOES NOT RETURN: the
             *        terminal is restored and the process leaves with exitCode
             *        without running static destructors. That is so whether or
             *        not a console writer was ever started -- including for
             *        Console.Style = "plain" and for a Windows service, which
             *        then misses its SERVICE_STOPPED handshake. An SCM restart
             *        is recoverable; a live producer writing into a
             *        half-destructed Log is not.
             * @param exitCode the code the process should leave with. Passed
             *        rather than assumed so a scheduled RESTART still exits 2
             *        even on the emergency path.
             */
            void Finish(bool producersQuiesced, int exitCode);

            /**
             * @brief Give the terminal back, now, without unwinding anything
             *        else.
             *
             * Idempotent, total and non-throwing. ConsoleUI::Stop() is best
             * effort inside a catch(...) -- it does an allocating tail.assign
             * (ConsoleUI.cpp:310) before Terminal::Leave() (:313), so a
             * bad_alloc can escape it with the terminal still captured -- and
             * MaNGOS::Console::Terminal::Leave() then runs unconditionally as
             * the final fallback. Leave() is safe after a failed or absent
             * Enter() (Terminal.h:96). Called by Finish(), by the destructor,
             * and by the emergency path.
             */
            void RestoreTerminal();

            /**
             * @brief Consume pending keystrokes and throw them away.
             *
             * realmd reads no commands. This exists purely for a side effect:
             * ConsoleUI::PollInput() is the only consumer of Windows key
             * records, and Terminal::Leave() does not call
             * FlushConsoleInputBuffer(), so without this everything typed while
             * realmd ran is replayed into the operator's shell after realmd
             * exits. POSIX self-cleans via TCSAFLUSH; the call is harmless
             * there.
             *
             * Call from the housekeeping thread only: PollInput() is the single
             * entry point that touches stdin.
             */
            void DrainInput();

        private:
            /**
             * @brief The invariant, in one place. Never returns.
             *
             * Flushes the log, halts the SQL delay thread for its FLUSH of
             * queued async writes, restores the terminal and leaves the
             * process. Each step before the restoration is best effort inside
             * its OWN catch(...) -- separately, so a throwing log flush cannot
             * skip the database flush -- because this is called from exception
             * handlers, where an escaping throw is std::terminate with the
             * alternate screen up.
             */
            [[noreturn]] void LeaveNow();

            bool m_writerRunning;
            bool m_uiActive;
            bool m_finished;
            int  m_exitCode;
    };
}

#endif
/// @}
