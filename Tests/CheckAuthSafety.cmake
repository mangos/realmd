file(READ "${REALMD_SOURCE}/Auth/AuthSocket.cpp" AUTH_SOCKET)

function(require_count HAYSTACK NEEDLE EXPECTED MESSAGE_TEXT)
  string(REGEX MATCHALL "${NEEDLE}" MATCHES "${HAYSTACK}")
  list(LENGTH MATCHES ACTUAL)
  if(NOT ACTUAL EQUAL EXPECTED)
    message(FATAL_ERROR
      "${MESSAGE_TEXT}: expected ${EXPECTED}, found ${ACTUAL}")
  endif()
endfunction()

foreach(FORBIDDEN_TEXT "FLUSH TABLES" "keyVerified" "GetExactLocaleName")
  string(FIND "${AUTH_SOCKET}" "${FORBIDDEN_TEXT}" POSITION)
  if(NOT POSITION EQUAL -1)
    message(FATAL_ERROR
      "Forbidden auth publication pattern remains: ${FORBIDDEN_TEXT}")
  endif()
endforeach()

require_count("${AUTH_SOCKET}" "ParseClientLocaleClaim" 2
  "Both authentication challenges must parse the exact locale claim")
require_count("${AUTH_SOCKET}" "NULLIF\\('%s',[ ]*''\\)" 2
  "Both successful proof paths must publish invalid locale as SQL NULL")
require_count("${AUTH_SOCKET}" "if \\(\\!LoginDatabase\\.DirectPExecute\\(" 2
  "Both successful proof paths must check locale publication synchronously")

string(FIND "${AUTH_SOCKET}"
  "if (!LoginDatabase.DirectPExecute(" CHECKED_UPDATE)
if(CHECKED_UPDATE EQUAL -1)
  message(FATAL_ERROR "Session-key update is not checked synchronously")
endif()

string(FIND "${AUTH_SOCKET}" "SendProof(sha);" SEND_PROOF)
if(SEND_PROOF EQUAL -1 OR CHECKED_UPDATE GREATER SEND_PROOF)
  message(FATAL_ERROR
    "Successful proof can precede checked session-key publication")
endif()

string(FIND "${AUTH_SOCKET}"
  "bool AuthSocket::_HandleReconnectProof()" RECONNECT_PROOF)
if(RECONNECT_PROOF EQUAL -1)
  message(FATAL_ERROR "Reconnect proof handler is missing")
endif()
string(SUBSTRING "${AUTH_SOCKET}" ${RECONNECT_PROOF} -1 RECONNECT_BODY)
string(FIND "${RECONNECT_BODY}"
  "if (!LoginDatabase.DirectPExecute(" RECONNECT_UPDATE)
string(FIND "${RECONNECT_BODY}"
  "send((char const*)pkt.contents(), pkt.size());" RECONNECT_SUCCESS)
if(RECONNECT_UPDATE EQUAL -1 OR RECONNECT_SUCCESS EQUAL -1 OR
   RECONNECT_UPDATE GREATER RECONNECT_SUCCESS)
  message(FATAL_ERROR
    "Reconnect success can precede checked locale and OS publication")
endif()
