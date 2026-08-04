file(READ "${REALMD_SOURCE}/realmd.conf.dist.in" REALMD_CONFIG)

# Every key realmd reads has to be both documented and set. A key that is only
# set is invisible to an operator; a key that is only documented is a lie.
foreach(REQUIRED_TEXT
    "#    Console.Style"
    "#    Console.Scrollback"
    "Console.Style          = \"auto\""
    "Console.Scrollback     = 20000")
  string(FIND "${REALMD_CONFIG}" "${REQUIRED_TEXT}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR
      "realmd.conf.dist.in is missing required text: ${REQUIRED_TEXT}")
  endif()
endforeach()

# The console settings belong to the logging group, between LogColors and
# UseProcessors, in both the doc block and the value block.
string(FIND "${REALMD_CONFIG}" "#    LogColors" DOC_LOGCOLORS)
string(FIND "${REALMD_CONFIG}" "#    Console.Style" DOC_CONSOLE)
string(FIND "${REALMD_CONFIG}" "#    UseProcessors" DOC_PROCESSORS)
if(DOC_CONSOLE LESS DOC_LOGCOLORS OR DOC_PROCESSORS LESS DOC_CONSOLE)
  message(FATAL_ERROR
    "The console doc block must sit in the logging group, after LogColors")
endif()

string(FIND "${REALMD_CONFIG}" "LogColors              =" VAL_LOGCOLORS)
string(FIND "${REALMD_CONFIG}" "Console.Style          =" VAL_CONSOLE)
string(FIND "${REALMD_CONFIG}" "UseProcessors          =" VAL_PROCESSORS)
if(VAL_CONSOLE LESS VAL_LOGCOLORS OR VAL_PROCESSORS LESS VAL_CONSOLE)
  message(FATAL_ERROR
    "The console values must sit in the logging value group, after LogColors")
endif()

# Documented defaults that contradicted the code, and one annotation that
# inverted the meaning of a banning control.
foreach(FORBIDDEN_TEXT
    "Default: 3  (Never ban)"
    "Default: 300"
    "Default: 1 (HIGH)"
    "Default: \"Realmd.log\""
    "for example :)")
  string(FIND "${REALMD_CONFIG}" "${FORBIDDEN_TEXT}" POSITION)
  if(NOT POSITION EQUAL -1)
    message(FATAL_ERROR
      "realmd.conf.dist.in still documents a wrong default: ${FORBIDDEN_TEXT}")
  endif()
endforeach()

foreach(REQUIRED_TEXT
    "Default: 0 (Never ban)"
    "Default: 600"
    "Default: 0 (Normal)"
    "#    DBErrorLogFile"
    "#    ConfVersion"
    "DBErrorLogFile         = \"\"")
  string(FIND "${REALMD_CONFIG}" "${REQUIRED_TEXT}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR
      "realmd.conf.dist.in is missing required text: ${REQUIRED_TEXT}")
  endif()
endforeach()
