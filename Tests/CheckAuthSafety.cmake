file(READ "${REALMD_SOURCE}/Auth/AuthSocket.cpp" AUTH_SOCKET)

foreach(FORBIDDEN_TEXT "FLUSH TABLES" "keyVerified" "GetExactLocaleName"
    "EndianConvert(*((uint32*)(&ch->os[0])))")
  string(FIND "${AUTH_SOCKET}" "${FORBIDDEN_TEXT}" POSITION)
  if(NOT POSITION EQUAL -1)
    message(FATAL_ERROR
      "Forbidden auth publication pattern remains: ${FORBIDDEN_TEXT}")
  endif()
endforeach()

string(FIND "${AUTH_SOCKET}"
  "bool AuthSocket::_HandleLogonChallenge()" LOGON_CHALLENGE)
string(FIND "${AUTH_SOCKET}"
  "bool AuthSocket::_HandleLogonProof()" LOGON_PROOF)
string(FIND "${AUTH_SOCKET}"
  "bool AuthSocket::_HandleReconnectChallenge()" RECONNECT_CHALLENGE)
string(FIND "${AUTH_SOCKET}"
  "bool AuthSocket::_HandleReconnectProof()" RECONNECT_PROOF)
if(LOGON_CHALLENGE EQUAL -1 OR LOGON_PROOF EQUAL -1 OR
   RECONNECT_CHALLENGE EQUAL -1 OR RECONNECT_PROOF EQUAL -1)
  message(FATAL_ERROR "An authentication handler is missing")
endif()

math(EXPR LOGON_CHALLENGE_LENGTH "${LOGON_PROOF} - ${LOGON_CHALLENGE}")
string(SUBSTRING "${AUTH_SOCKET}" ${LOGON_CHALLENGE}
  ${LOGON_CHALLENGE_LENGTH} LOGON_CHALLENGE_BODY)
string(FIND "${LOGON_CHALLENGE_BODY}"
  "ParseClientLocaleClaim" LOGON_PARSE)
if(LOGON_PARSE EQUAL -1)
  message(FATAL_ERROR "Logon challenge does not parse the exact locale claim")
endif()

math(EXPR LOGON_PROOF_LENGTH "${RECONNECT_CHALLENGE} - ${LOGON_PROOF}")
string(SUBSTRING "${AUTH_SOCKET}" ${LOGON_PROOF}
  ${LOGON_PROOF_LENGTH} LOGON_PROOF_BODY)
string(FIND "${LOGON_PROOF_BODY}"
  "if (!LoginDatabase.DirectPExecute(" LOGON_UPDATE)
string(FIND "${LOGON_PROOF_BODY}"
  "`client_locale` = NULLIF('%s', '')" LOGON_NULL_LOCALE)
string(FIND "${LOGON_PROOF_BODY}" "SendProof(sha);" LOGON_SUCCESS)
if(LOGON_UPDATE EQUAL -1 OR LOGON_NULL_LOCALE EQUAL -1 OR
   LOGON_SUCCESS EQUAL -1 OR LOGON_UPDATE GREATER LOGON_NULL_LOCALE OR
   LOGON_NULL_LOCALE GREATER LOGON_SUCCESS)
  message(FATAL_ERROR
    "Logon success can precede checked nullable locale publication")
endif()

math(EXPR RECONNECT_CHALLENGE_LENGTH
  "${RECONNECT_PROOF} - ${RECONNECT_CHALLENGE}")
string(SUBSTRING "${AUTH_SOCKET}" ${RECONNECT_CHALLENGE}
  ${RECONNECT_CHALLENGE_LENGTH} RECONNECT_CHALLENGE_BODY)
string(FIND "${RECONNECT_CHALLENGE_BODY}"
  "ParseClientLocaleClaim" RECONNECT_PARSE)
if(RECONNECT_PARSE EQUAL -1)
  message(FATAL_ERROR
    "Reconnect challenge does not parse the exact locale claim")
endif()

string(SUBSTRING "${AUTH_SOCKET}" ${RECONNECT_PROOF} -1 RECONNECT_BODY)
string(FIND "${RECONNECT_BODY}"
  "if (!LoginDatabase.DirectPExecute(" RECONNECT_UPDATE)
string(FIND "${RECONNECT_BODY}"
  "`client_locale` = NULLIF('%s', '')" RECONNECT_NULL_LOCALE)
string(FIND "${RECONNECT_BODY}"
  "send((char const*)pkt.contents(), pkt.size());" RECONNECT_SUCCESS)
if(RECONNECT_UPDATE EQUAL -1 OR RECONNECT_NULL_LOCALE EQUAL -1 OR
   RECONNECT_SUCCESS EQUAL -1 OR
   RECONNECT_UPDATE GREATER RECONNECT_NULL_LOCALE OR
   RECONNECT_NULL_LOCALE GREATER RECONNECT_SUCCESS)
  message(FATAL_ERROR
    "Reconnect success can precede checked nullable locale publication")
endif()
