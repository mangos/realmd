file(READ "${REALMD_SOURCE}/Auth/AuthSocket.cpp" AUTH_SOCKET)

foreach(FORBIDDEN_TEXT "FLUSH TABLES" "keyVerified")
  string(FIND "${AUTH_SOCKET}" "${FORBIDDEN_TEXT}" POSITION)
  if(NOT POSITION EQUAL -1)
    message(FATAL_ERROR
      "Forbidden auth publication pattern remains: ${FORBIDDEN_TEXT}")
  endif()
endforeach()

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
