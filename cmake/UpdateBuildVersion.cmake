# UpdateBuildVersion.cmake
#
# Generates a header with a timestamp-based build id.
# Intended to be run at build time (not configure time).
#
# Required -D variables:
#   OUT_HEADER: path to generated header
# Optional:
#   MAJOR (default 1)
#   MINOR (default 0)
#   TIMESTAMP_FORMAT (default %Y%m%d%H%M)

if(NOT DEFINED OUT_HEADER)
  message(FATAL_ERROR "OUT_HEADER is required")
endif()

if(NOT DEFINED MAJOR)
  set(MAJOR 1)
endif()
if(NOT DEFINED MINOR)
  set(MINOR 0)
endif()
if(NOT DEFINED TIMESTAMP_FORMAT)
  set(TIMESTAMP_FORMAT "%Y%m%d%H%M")
endif()

string(TIMESTAMP _ts "${TIMESTAMP_FORMAT}")
set(VERSION_STRING "${MAJOR}.${MINOR}.${_ts}")

get_filename_component(_out_dir "${OUT_HEADER}" DIRECTORY)
if(_out_dir)
  file(MAKE_DIRECTORY "${_out_dir}")
endif()

file(WRITE "${OUT_HEADER}" "#pragma once\n\n#define HAIRTOOL_VERSION_MAJOR ${MAJOR}\n#define HAIRTOOL_VERSION_MINOR ${MINOR}\n#define HAIRTOOL_VERSION_BUILD ${_ts}\n#define HAIRTOOL_VERSION_STRING \"${VERSION_STRING}\"\n")

