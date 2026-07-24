file(READ "${REALMD_SOURCE}/Auth/AuthSocket.cpp" AUTH_SOCKET)
file(READ "${REALMD_SOURCE}/Auth/AuthSocket.h" AUTH_SOCKET_HEADER)
file(READ "${REALMD_SOURCE}/Auth/PatchHandler.cpp" PATCH_HANDLER)
file(READ "${REALMD_SOURCE}/Auth/PatchHandler.h" PATCH_HANDLER_HEADER)
file(READ "${REALMD_SOURCE}/Main.cpp" MAIN_SOURCE)
file(READ "${REALMD_SOURCE}/realmd.conf.dist.in" REALMD_CONFIG)

set(PATCH_SOURCES
  "${AUTH_SOCKET}\n${AUTH_SOCKET_HEADER}\n${PATCH_HANDLER}\n${PATCH_HANDLER_HEADER}")

foreach(FORBIDDEN_TEXT
    "class PatchCache"
    "GetMD5("
    "_patchPath"
    "StartPatchTransfer(m_sender, m_closer, m_flowControl, _patchPath")
  string(FIND "${PATCH_SOURCES}" "${FORBIDDEN_TEXT}" POSITION)
  if(NOT POSITION EQUAL -1)
    message(FATAL_ERROR
      "Forbidden patch delivery pattern remains: ${FORBIDDEN_TEXT}")
  endif()
endforeach()

foreach(REQUIRED_TEXT
    "std::unique_ptr<PatchArtifact>"
    "startOffset > artifact->size()"
    "PatchArtifact::Open"
    "_patchArtifact")
  string(FIND "${PATCH_SOURCES}" "${REQUIRED_TEXT}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR
      "Required retained-artifact pattern is missing: ${REQUIRED_TEXT}")
  endif()
endforeach()

string(FIND "${AUTH_SOCKET}"
  "if (!memcmp(M.AsByteArray(), lp.M1, 20))" VERIFIED_PROOF)
string(FIND "${AUTH_SOCKET}"
  "_patchPolicy.ShouldPatch" PATCH_DECISION)
if(VERIFIED_PROOF EQUAL -1 OR
   PATCH_DECISION EQUAL -1 OR
   PATCH_DECISION LESS VERIFIED_PROOF)
  message(FATAL_ERROR
    "Patch selection must occur after successful SRP proof verification")
endif()

string(FIND "${AUTH_SOCKET}" "if (!artifact)" MISSING_ARTIFACT)
if(MISSING_ARTIFACT EQUAL -1)
  message(FATAL_ERROR "Missing patch artifacts are not handled explicitly")
endif()
string(SUBSTRING "${AUTH_SOCKET}" ${MISSING_ARTIFACT} 500 MISSING_BRANCH)
string(FIND "${MISSING_BRANCH}" "SendInvalidVersion();" INVALID_VERSION)
string(FIND "${MISSING_BRANCH}" "return true;" BRANCH_RETURN)
if(INVALID_VERSION EQUAL -1 OR
   BRANCH_RETURN EQUAL -1 OR
   INVALID_VERSION GREATER BRANCH_RETURN)
  message(FATAL_ERROR
    "Missing required patches must send invalid-version before returning")
endif()

string(FIND "${MAIN_SOURCE}" "Patch.ForceBuilds" FORCE_CONFIG_READ)
string(FIND "${REALMD_CONFIG}" "Patch.ForceBuilds" FORCE_CONFIG_DOC)
if(FORCE_CONFIG_READ EQUAL -1 OR FORCE_CONFIG_DOC EQUAL -1)
  message(FATAL_ERROR "Patch.ForceBuilds is not wired and documented")
endif()
