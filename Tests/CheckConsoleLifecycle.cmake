file(READ "${REALMD_SOURCE}/Console/ConsoleLifecycle.cpp" CONSOLE_LIFECYCLE)

# Slice ONE method's body out of the file: from its own definition up to the
# definition that follows it, never to end-of-file. The bound is the whole
# point. An earlier revision searched from the destructor to EOF, so a call
# appearing later -- inside Finish(), inside RestoreTerminal() -- satisfied an
# assertion written about the destructor, and a broken destructor passed the
# check written to catch exactly that.
function(realmd_method_body OUT_VAR SOURCE_VAR BEGIN_TEXT END_TEXT)
  string(FIND "${${SOURCE_VAR}}" "${BEGIN_TEXT}" BEGIN_AT)
  if(BEGIN_AT EQUAL -1)
    message(FATAL_ERROR
      "Console/ConsoleLifecycle.cpp does not define: ${BEGIN_TEXT}")
  endif()
  string(FIND "${${SOURCE_VAR}}" "${END_TEXT}" END_AT)
  if(END_AT EQUAL -1 OR END_AT LESS BEGIN_AT)
    message(FATAL_ERROR
      "Console/ConsoleLifecycle.cpp must define ${END_TEXT} after ${BEGIN_TEXT}")
  endif()
  math(EXPR BODY_LENGTH "${END_AT} - ${BEGIN_AT}")
  string(SUBSTRING "${${SOURCE_VAR}}" ${BEGIN_AT} ${BODY_LENGTH} BODY)
  set(${OUT_VAR} "${BODY}" PARENT_SCOPE)
endfunction()

# Presence first. Every literal below is load-bearing in a position test, and a
# missing string compares as -1, which a position test would then report as an
# ordering failure for something that is simply not there.
foreach(REQUIRED_TEXT
    "void ConsoleLifecycle::LeaveNow()"
    "ConsoleLifecycle::ConsoleLifecycle()"
    "ConsoleLifecycle::~ConsoleLifecycle()"
    "void ConsoleLifecycle::Finish("
    "void ConsoleLifecycle::RestoreTerminal()"
    "void ConsoleLifecycle::DrainInput()"
    "#include \"Console/Terminal.h\""
    "sLog.StartConsoleThread();"
    "sLog.StopConsoleThread();"
    "sLog.Flush();"
    "LoginDatabase.HaltDelayThread();"
    "ConsoleUI::Instance().Start("
    "ConsoleUI::Instance().Stop();"
    "MaNGOS::Console::Terminal::IsInteractive()"
    "MaNGOS::Console::Terminal::Leave();"
    "std::_Exit(m_exitCode);"
    "fflush(NULL);"
    "m_writerRunning = true;"
    "m_finished = true;"
    "if (!producersQuiesced)"
    "if (!m_finished)"
    "catch (...)"
    "SetPrompt(\"\")"
    "PollInput(discarded)"
    "SetScrollback("
    "GetStringDefault(\"Console.Style\""
    "GetIntDefault(\"Console.Scrollback\"")
  string(FIND "${CONSOLE_LIFECYCLE}" "${REQUIRED_TEXT}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR
      "Console/ConsoleLifecycle.cpp is missing required text: ${REQUIRED_TEXT}")
  endif()
endforeach()

# The emergency exit is conditioned on the PRODUCERS alone. Gating it on the
# writer was the blocking defect: the writer is not the only console log
# producer. A detached patch-transfer closure calls sLog.outError
# (Auth/PatchHandler.cpp:107) and ~Log() closes the log files during static
# destruction (shared/Log/Log.h:199-202), so a "plain" or service run -- which
# never starts a writer -- would return into static destruction underneath one.
string(FIND "${CONSOLE_LIFECYCLE}"
  "m_writerRunning && !producersQuiesced" WRITER_GATED)
if(NOT WRITER_GATED EQUAL -1)
  message(FATAL_ERROR
    "The emergency exit must be conditioned on !producersQuiesced alone, never "
    "on the console writer having been started")
endif()

# The constructor must NOT rethrow. An object whose constructor throws was never
# constructed, so ~ConsoleLifecycle never runs: rethrowing unwinds out of main
# with the producers unproven and the terminal still captured. The catch takes
# the emergency path instead, and this assertion is what stops the rethrow
# coming back.
string(FIND "${CONSOLE_LIFECYCLE}" "throw;" RETHROW)
if(NOT RETHROW EQUAL -1)
  message(FATAL_ERROR
    "The ConsoleLifecycle constructor must never rethrow: it must take the "
    "no-static-destruction path instead")
endif()

# realmd drives no scroll keys, so the hint must not copy mangosd's PgUp/PgDn
# text: advertising a key the daemon does not implement is a lie to the
# operator.
string(FIND "${CONSOLE_LIFECYCLE}" "PgUp" MANGOSD_HINT)
if(NOT MANGOSD_HINT EQUAL -1)
  message(FATAL_ERROR
    "The realmd console hint must not copy mangosd's PgUp/PgDn scroll text")
endif()

# Every assertion from here on reads ONE bounded method body, never the whole
# file. The bounds also pin the definition order: LeaveNow, the constructor, the
# destructor, Finish, RestoreTerminal, DrainInput.
realmd_method_body(LEAVE_BODY CONSOLE_LIFECYCLE
  "void ConsoleLifecycle::LeaveNow()"
  "ConsoleLifecycle::ConsoleLifecycle()")
realmd_method_body(CTOR_BODY CONSOLE_LIFECYCLE
  "ConsoleLifecycle::ConsoleLifecycle()"
  "ConsoleLifecycle::~ConsoleLifecycle()")
realmd_method_body(DTOR_BODY CONSOLE_LIFECYCLE
  "ConsoleLifecycle::~ConsoleLifecycle()"
  "void ConsoleLifecycle::Finish(")
realmd_method_body(FINISH_BODY CONSOLE_LIFECYCLE
  "void ConsoleLifecycle::Finish("
  "void ConsoleLifecycle::RestoreTerminal()")
realmd_method_body(RESTORE_BODY CONSOLE_LIFECYCLE
  "void ConsoleLifecycle::RestoreTerminal()"
  "void ConsoleLifecycle::DrainInput()")

# LeaveNow(): flush the log, flush the queued async DB writes (the exit runs no
# destructors and no atexit handlers, so they would otherwise be discarded),
# give the terminal back, flush stdio, leave. The catch REQUIRED between the two
# flushes is what proves they sit in separate try blocks: sharing one meant a
# throwing sLog.Flush() skipped the database halt outright.
string(FIND "${LEAVE_BODY}" "sLog.Flush();" LEAVE_FLUSH)
string(FIND "${LEAVE_BODY}" "catch (...)" LEAVE_FLUSH_CATCH)
string(FIND "${LEAVE_BODY}" "LoginDatabase.HaltDelayThread();" LEAVE_HALT)
string(FIND "${LEAVE_BODY}" "RestoreTerminal();" LEAVE_RESTORE)
string(FIND "${LEAVE_BODY}" "std::_Exit(m_exitCode);" LEAVE_EXIT)
if(LEAVE_FLUSH EQUAL -1 OR LEAVE_FLUSH_CATCH EQUAL -1 OR LEAVE_HALT EQUAL -1 OR
   LEAVE_RESTORE EQUAL -1 OR LEAVE_EXIT EQUAL -1)
  message(FATAL_ERROR
    "LeaveNow() must run sLog.Flush, LoginDatabase.HaltDelayThread, "
    "RestoreTerminal and then the exit, and must guard the flushes")
endif()
if(LEAVE_FLUSH_CATCH LESS LEAVE_FLUSH OR LEAVE_HALT LESS LEAVE_FLUSH_CATCH OR
   LEAVE_RESTORE LESS LEAVE_HALT OR LEAVE_EXIT LESS LEAVE_RESTORE)
  message(FATAL_ERROR
    "LeaveNow() order must be: sLog.Flush, catch (...), "
    "LoginDatabase.HaltDelayThread, RestoreTerminal, then the exit")
endif()

# ...and the database halt needs a catch of its own, or a throw from it skips
# the terminal restoration that the whole emergency path exists to perform.
string(SUBSTRING "${LEAVE_BODY}" ${LEAVE_HALT} -1 LEAVE_TAIL)
string(FIND "${LEAVE_TAIL}" "catch (...)" TAIL_CATCH)
string(FIND "${LEAVE_TAIL}" "RestoreTerminal();" TAIL_RESTORE)
if(TAIL_CATCH EQUAL -1 OR TAIL_RESTORE LESS TAIL_CATCH)
  message(FATAL_ERROR
    "LoginDatabase.HaltDelayThread() in LeaveNow() must have its own "
    "catch (...) between it and RestoreTerminal()")
endif()

# The constructor gate, and it is the RESULT of the probe that must control an
# early return. The check this replaces only required that SOME IsInteractive()
# call appeared before m_writerRunning was raised, which a constructor that
# ignored the answer satisfied trivially. So the exact condition is required,
# and a return between it and the writer flag with it.
string(FIND "${CTOR_BODY}" "GetStringDefault(\"Console.Style\"" CTOR_STYLE)
string(FIND "${CTOR_BODY}"
  "if (style == \"plain\" || !MaNGOS::Console::Terminal::IsInteractive())"
  CTOR_GATE)
string(FIND "${CTOR_BODY}" "return;" CTOR_RETURN)
string(FIND "${CTOR_BODY}" "m_writerRunning = true;" CTOR_FLAG)
string(FIND "${CTOR_BODY}" "sLog.StartConsoleThread();" CTOR_START)
string(FIND "${CTOR_BODY}" "ConsoleUI::Instance().Start(" CTOR_UI)
if(CTOR_STYLE EQUAL -1 OR CTOR_GATE EQUAL -1 OR CTOR_RETURN EQUAL -1 OR
   CTOR_FLAG EQUAL -1 OR CTOR_START EQUAL -1 OR CTOR_UI EQUAL -1)
  message(FATAL_ERROR
    "The constructor must read Console.Style, gate on exactly "
    "if (style == \"plain\" || !MaNGOS::Console::Terminal::IsInteractive()), "
    "return from that gate, and only then start the writer and the display")
endif()
if(CTOR_GATE LESS CTOR_STYLE OR CTOR_RETURN LESS CTOR_GATE OR
   CTOR_FLAG LESS CTOR_RETURN OR CTOR_START LESS CTOR_FLAG OR
   CTOR_UI LESS CTOR_START)
  message(FATAL_ERROR
    "Constructor order must be: read Console.Style, the plain/non-interactive "
    "condition, its return, m_writerRunning = true, sLog.StartConsoleThread(), "
    "then ConsoleUI::Start() -- the writer is the sole caller of Render(), so a "
    "display started first paints an empty frame under the synchronous log")
endif()

# The destructor is the backstop, read from its OWN body. Reaching it without a
# completed Finish() means nothing ever proved the producers were joined, so it
# must take the same emergency path rather than returning into static
# destruction. It tests the PROOF (m_finished), not the writer, for the same
# reason the emergency condition does.
string(FIND "${DTOR_BODY}" "if (!m_finished)" DTOR_PROOF)
string(FIND "${DTOR_BODY}" "LeaveNow();" DTOR_LEAVE)
string(FIND "${DTOR_BODY}" "RestoreTerminal();" DTOR_RESTORE)
if(DTOR_PROOF EQUAL -1 OR DTOR_LEAVE EQUAL -1 OR DTOR_RESTORE EQUAL -1)
  message(FATAL_ERROR
    "The destructor itself must test m_finished, call LeaveNow() and restore")
endif()
if(DTOR_LEAVE LESS DTOR_PROOF OR DTOR_RESTORE LESS DTOR_LEAVE)
  message(FATAL_ERROR
    "Destructor order must be: m_finished test, LeaveNow, RestoreTerminal")
endif()

# Finish(): decide on the emergency path first, then stop the writer, then give
# the terminal back -- ConsoleUI::Stop() last, so no repaint can race
# Terminal::Leave() -- and only then record the proof the destructor reads.
string(FIND "${FINISH_BODY}" "if (!producersQuiesced)" FINISH_QUIESCED)
string(FIND "${FINISH_BODY}" "LeaveNow();" FINISH_LEAVE)
string(FIND "${FINISH_BODY}" "sLog.StopConsoleThread();" FINISH_STOP)
string(FIND "${FINISH_BODY}" "RestoreTerminal();" FINISH_RESTORE)
string(FIND "${FINISH_BODY}" "m_finished = true;" FINISH_PROOF)
if(FINISH_QUIESCED EQUAL -1 OR FINISH_LEAVE EQUAL -1 OR FINISH_STOP EQUAL -1 OR
   FINISH_RESTORE EQUAL -1 OR FINISH_PROOF EQUAL -1)
  message(FATAL_ERROR
    "Finish() must test !producersQuiesced, leave from it, stop the writer, "
    "restore the terminal and only then set m_finished")
endif()
if(FINISH_LEAVE LESS FINISH_QUIESCED OR FINISH_STOP LESS FINISH_LEAVE OR
   FINISH_RESTORE LESS FINISH_STOP OR FINISH_PROOF LESS FINISH_RESTORE)
  message(FATAL_ERROR
    "Finish() order must be: !producersQuiesced test, LeaveNow, "
    "StopConsoleThread, RestoreTerminal, m_finished = true")
endif()

# Terminal restoration is total and non-throwing. ConsoleUI::Stop() does an
# allocating tail.assign (shared/Console/ConsoleUI.cpp:310) BEFORE
# Terminal::Leave() (:313), so a bad_alloc can escape it with the terminal still
# captured; and ConsoleUI::Start() calls Terminal::Enter() (:286) before it sets
# m_active (:293), so a throw in between captures the terminal without ConsoleUI
# ever believing it did. Stop() is therefore best effort inside a catch(...),
# and Terminal::Leave() -- documented and verified safe after a failed or absent
# Enter() (shared/Console/Terminal.h:96, Terminal.cpp:259 and :379) -- is the
# unconditional final fallback.
string(FIND "${RESTORE_BODY}" "ConsoleUI::Instance().Stop();" RESTORE_STOP)
string(FIND "${RESTORE_BODY}" "catch (...)" RESTORE_CATCH)
string(FIND "${RESTORE_BODY}" "MaNGOS::Console::Terminal::Leave();"
  RESTORE_LEAVE)
if(RESTORE_STOP EQUAL -1 OR RESTORE_CATCH EQUAL -1 OR RESTORE_LEAVE EQUAL -1)
  message(FATAL_ERROR
    "RestoreTerminal() must call ConsoleUI::Stop() inside try/catch(...) and "
    "then Terminal::Leave() unconditionally")
endif()
if(RESTORE_CATCH LESS RESTORE_STOP OR RESTORE_LEAVE LESS RESTORE_CATCH)
  message(FATAL_ERROR
    "RestoreTerminal() order must be: ConsoleUI::Stop(), catch (...), then "
    "Terminal::Leave()")
endif()

file(READ "${REALMD_SOURCE}/Main.cpp" MAIN_SOURCE)

# The console is owned by the scope guard. A direct call in Main.cpp would be
# silently defeated by any early return added between the pair, and a second
# process-exit call would be a second implementation of the invariant -- the
# thing that has to stay in exactly one place to stay checkable. This matches on
# text, so a COMMENT quoting the exit call fails it too; that is intentional.
foreach(FORBIDDEN_TEXT
    "sLog.StartConsoleThread"
    "sLog.StopConsoleThread"
    "ConsoleUI::Instance().Start"
    "ConsoleUI::Instance().Stop"
    "std::_Exit")
  string(FIND "${MAIN_SOURCE}" "${FORBIDDEN_TEXT}" POSITION)
  if(NOT POSITION EQUAL -1)
    message(FATAL_ERROR
      "Main.cpp must not drive the console or process exit directly: "
      "${FORBIDDEN_TEXT}")
  endif()
endforeach()

foreach(REQUIRED_TEXT
    "MaNGOS::Realmd::ConsoleLifecycle console;"
    "console.DrainInput();"
    "MaNGOS::Realmd::CancelPatchTransfers();"
    "bool transfersDrained = false;"
    "catch (...)"
    "console.Finish(false, exitCode);"
    "console.Finish(transfersDrained, exitCode);")
  string(FIND "${MAIN_SOURCE}" "${REQUIRED_TEXT}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR
      "Main.cpp is missing required console wiring: ${REQUIRED_TEXT}")
  endif()
endforeach()

# Startup placement: below HookSignals(), and below the last
# Log::WaitBeforeContinueIfNeed site, so none of the six early error paths can
# print invisibly and block on stdin underneath the alternate screen, and so no
# thread is started before startDaemon() forks.
string(FIND "${MAIN_SOURCE}" "    HookSignals();" HOOK_SIGNALS)
string(FIND "${MAIN_SOURCE}" "MaNGOS::Realmd::ConsoleLifecycle console;"
  CONSOLE_GUARD)
string(FIND "${MAIN_SOURCE}" "Log::WaitBeforeContinueIfNeed();" LAST_WAIT
  REVERSE)
if(HOOK_SIGNALS EQUAL -1)
  message(FATAL_ERROR "HookSignals() is no longer where the guard anchors")
endif()
if(CONSOLE_GUARD LESS HOOK_SIGNALS OR CONSOLE_GUARD LESS LAST_WAIT)
  message(FATAL_ERROR
    "The console guard must be constructed after HookSignals() and after every "
    "Log::WaitBeforeContinueIfNeed site")
endif()

# Shutdown order, and the reason for it. authServer.Stop() runs FIRST because it
# is what actually releases a transfer parked on backpressure: the transport's
# stop() disarms every send channel and disarm()'s out.close() wakes the
# producer. CancelPatchTransfers() is cleanup layered on top of that -- a
# net::Closer records close intent and does not force a close -- and it MUST NOT
# be hoisted above the stop, because IOCP's requestClose() dereferences a raw
# ConnCtx* after releasing the channel mutex (shared/net/iocp/IocpServer.cpp:59-
# 78) and is safe only against an already-disarmed channel. The drain then
# proves the producers are gone, and the console is finalised BEFORE
# UnhookSignals() so the handlers are still installed while the terminal is
# restored -- a Ctrl-C in that window would otherwise take the default action
# with the alternate screen still up.
string(FIND "${MAIN_SOURCE}" "authServer.Stop();" SERVER_STOP)
string(FIND "${MAIN_SOURCE}" "MaNGOS::Realmd::CancelPatchTransfers();" CANCEL)
string(FIND "${MAIN_SOURCE}" "MaNGOS::Realmd::DrainPatchTransfers(" DRAIN)
string(FIND "${MAIN_SOURCE}" "LoginDatabase.HaltDelayThread();" HALT_DB)
string(FIND "${MAIN_SOURCE}" "console.Finish(transfersDrained, exitCode);"
  FINISH)
string(FIND "${MAIN_SOURCE}" "    UnhookSignals();" UNHOOK)
if(SERVER_STOP EQUAL -1 OR CANCEL EQUAL -1 OR DRAIN EQUAL -1 OR
   HALT_DB EQUAL -1 OR FINISH EQUAL -1 OR UNHOOK EQUAL -1)
  message(FATAL_ERROR "The realmd shutdown sequence is incomplete")
endif()
if(CANCEL LESS SERVER_STOP OR DRAIN LESS CANCEL OR HALT_DB LESS DRAIN OR
   FINISH LESS HALT_DB OR UNHOOK LESS FINISH)
  message(FATAL_ERROR
    "Shutdown order must be: authServer.Stop, CancelPatchTransfers, "
    "DrainPatchTransfers, HaltDelayThread, console.Finish, UnhookSignals")
endif()

# The emergency route has to be non-throwing END TO END, and Main.cpp owns the
# half of it that runs before the guard is reached. Every step here can throw --
# authServer.Stop(), the cancellation, the drain, the diagnostic,
# HaltDelayThread() and sLog.Flush() -- and an exception unwinding past them
# reaches static destruction with the producers unproven and the terminal still
# captured. So the whole sequence sits in one try, its catch (...) routes
# straight to console.Finish(false, exitCode), and the proof itself DEFAULTS to
# false before the try rather than being assigned inside it.
#
# The bound is the LAST catch (...) against console.Finish(transfersDrained,
# ...): the outer catch has to come after the ordinary Finish() to cover it,
# and the last Finish(false, ...) has to come after that catch to be its body.
# Anchoring on the drain instead would be satisfied by the nested best-effort
# catch around the diagnostic, with the outer try/catch removed entirely.
string(FIND "${MAIN_SOURCE}" "bool transfersDrained = false;" DRAINED_DEFAULT)
string(FIND "${MAIN_SOURCE}" "catch (...)" SHUTDOWN_CATCH REVERSE)
string(FIND "${MAIN_SOURCE}" "console.Finish(false, exitCode);" FINISH_FALSE
  REVERSE)
if(DRAINED_DEFAULT GREATER SERVER_STOP OR SHUTDOWN_CATCH LESS FINISH OR
   FINISH_FALSE LESS SHUTDOWN_CATCH)
  message(FATAL_ERROR
    "Main.cpp must declare transfersDrained = false before authServer.Stop(), "
    "enclose the whole shutdown INCLUDING console.Finish(transfersDrained, "
    "exitCode) in one try, and route its catch (...) straight to "
    "console.Finish(false, exitCode)")
endif()
