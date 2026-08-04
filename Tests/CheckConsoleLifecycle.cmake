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
