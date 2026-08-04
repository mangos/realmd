file(READ "${REALMD_SOURCE}/Auth/AuthSocket.cpp" AUTH_SOCKET)

# Every terminal outcome must go through the per-connection latch. A direct
# CountLogonOutcome() call on the auth path would let one connection contribute
# more than one increment.
string(FIND "${AUTH_SOCKET}" "MaNGOS::Realmd::CountLogonOutcome(" DIRECT_COUNT)
if(NOT DIRECT_COUNT EQUAL -1)
  message(FATAL_ERROR
    "AuthSocket must record outcomes through m_logonOutcome.Record(), never "
    "through CountLogonOutcome()")
endif()

# One site per classified path: rejected challenge, logon ok, logon bad proof,
# reconnect rejected, reconnect ok, reconnect bad proof, build/patch.
string(REGEX MATCHALL "m_logonOutcome\\.Record\\(" RECORD_SITES "${AUTH_SOCKET}")
list(LENGTH RECORD_SITES RECORD_SITE_COUNT)
if(NOT RECORD_SITE_COUNT EQUAL 7)
  message(FATAL_ERROR
    "Expected 7 logon-outcome record sites in AuthSocket.cpp, found "
    "${RECORD_SITE_COUNT}")
endif()

foreach(REQUIRED_OUTCOME
    "LogonOutcome::Ok"
    "LogonOutcome::Rejected"
    "LogonOutcome::BadProof"
    "LogonOutcome::BuildPatch")
  string(FIND "${AUTH_SOCKET}" "${REQUIRED_OUTCOME}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR
      "Logon outcome category is never recorded: ${REQUIRED_OUTCOME}")
  endif()
endforeach()

# SendInvalidVersion answers BOTH an unsupported build and a patch archive that
# will not open. Counting inside it is what makes one site cover both causes.
string(REGEX MATCH
  "void AuthSocket::SendInvalidVersion\\(\\)[^}]*LogonOutcome::BuildPatch"
  INVALID_VERSION_COUNTED "${AUTH_SOCKET}")
if(NOT INVALID_VERSION_COUNTED)
  message(FATAL_ERROR
    "SendInvalidVersion() must record the build/patch outcome itself, so both "
    "the invalid-build and missing-archive callers are covered by one site")
endif()

# The rejected-challenge site tests the status the success path sets, not a
# specific error code, so a newly added rejection reason cannot silently stop
# being counted.
string(FIND "${AUTH_SOCKET}" "if (_status != STATUS_LOGON_PROOF)" STATUS_GATE)
if(STATUS_GATE EQUAL -1)
  message(FATAL_ERROR
    "The rejected-challenge outcome must be derived from _status, so every "
    "rejection branch is covered by one test")
endif()
