file(READ "${REALMD_SOURCE}/Auth/AuthSocket.cpp" AUTH_SOCKET)

# Every SRP6 quantity has a width the protocol fixes, and the client reads and hashes it
# at that width. Asking a BigNumber how long it happens to be answers with the minimal
# encoding, which is one byte shorter whenever the most significant byte is zero -- one
# login in 256 for a uniformly distributed value. The proof is then taken over a
# different byte string than the client used, a correct password is rejected, and the
# next attempt works. It looks exactly like a flaky connection, which is why it survived
# so long. Both spellings below derive a width from a value; neither may come back.
foreach(FORBIDDEN_TEXT "AsByteArray()" "UpdateBigNumbers")
  string(FIND "${AUTH_SOCKET}" "${FORBIDDEN_TEXT}" POSITION)
  if(NOT POSITION EQUAL -1)
    message(FATAL_ERROR
      "SRP6 width taken from a value instead of the protocol: ${FORBIDDEN_TEXT}")
  endif()
endforeach()

# The proof is twenty bytes because a SHA1 digest is. Comparing it against a buffer of
# whatever length the number serialised to reads past the end of that buffer, and the
# comparison is wrong on top of it.
string(FIND "${AUTH_SOCKET}"
  "memcmp(M.AsByteArray(SHA_DIGEST_LENGTH), lp.M1, SHA_DIGEST_LENGTH)" PROOF_COMPARE)
if(PROOF_COMPARE EQUAL -1)
  message(FATAL_ERROR "The client proof is not compared at the full digest width")
endif()
