cmake_minimum_required(VERSION 3.3)

project(fmidi VERSION 0.1 LANGUAGES C CXX)

set(cmake_MODULE_DIR "${PROJECT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${cmake_MODULE_DIR}")

option(FMIDI_ENABLE_DEBUG "enable debugging features" OFF)
option(FMIDI_PIC "enable position independent code" ON)
option(FMIDI_STATIC "build as static library" ON)

if(FMIDI_ENABLE_DEBUG)
  add_definitions("-DFMIDI_DEBUG=1")
endif()
if(FMIDI_STATIC)
  add_definitions("-DFMIDI_STATIC=1")
  set(fmidi_LIB_TYPE "STATIC")
else()
  set(fmidi_LIB_TYPE "SHARED")
endif()

include(ExtraCompilerFlags)
enable_gcc_warning(all)
enable_colors()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if(CMAKE_SYSTEM_NAME MATCHES Windows)
  add_definitions("-D_CRT_SECURE_NO_WARNINGS=1")
endif()

#####################
# FEATURE DETECTION #
#####################

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost OPTIONAL_COMPONENTS filesystem system)

set(fmidi-play_BUILD FALSE)
find_library(ev_LIBRARY ev)

include(FindPkgConfig)
find_package(Curses)
pkg_check_modules(rtmidi rtmidi)
if(NOT rtmidi_FOUND)
  message(STATUS "rtmidi is missing, NOT building program fmidi-play.")
elseif (NOT CURSES_FOUND)
  message(STATUS "curses is missing, NOT building program fmidi-play.")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT (Boost_FOUND AND Boost_FILESYSTEM_LIBRARY AND Boost_SYSTEM_LIBRARY))
  message(STATUS "Boost is missing, NOT building program fmidi-play.")
elseif(NOT ev_LIBRARY)
  message(STATUS "libev is missing, NOT building program fmidi-play.")
else()
  set(fmidi-play_BUILD TRUE)
endif()

include(CheckCSourceCompiles)
check_c_source_compiles("#include <fts.h>
int main() { return fts_close((FTS *)0); }" fmidi_HAVE_FTS)

set(fmidi-grep_BUILD FALSE)
if(NOT fmidi_HAVE_FTS)
  message(STATUS "fts is missing, NOT building program fmidi-grep.")
elseif(NOT Boost_FOUND)
  message(STATUS "Boost is missing, NOT building program fmidi-grep.")
else()
  set(fmidi-grep_BUILD TRUE)
endif()

##############
# THIRDPARTY #
##############

add_library(fmidi-fmt INTERFACE)
target_include_directories(fmidi-fmt INTERFACE thirdparty/fmt)
target_compile_definitions(fmidi-fmt INTERFACE "FMT_HEADER_ONLY=1")

#############
# LIBRARIES #
#############

set(fmidi_SOURCES
  sources/fmidi/u_memstream.cc
  sources/fmidi/file/read_smf.cc
  sources/fmidi/file/write_smf.cc
  sources/fmidi/file/read_xmi.cc
  sources/fmidi/file/identify.cc
  sources/fmidi/fmidi_internal.cc
  sources/fmidi/fmidi_seq.cc
  sources/fmidi/fmidi_util.cc
  sources/fmidi/fmidi_player.cc)

add_library(fmidi "${fmidi_LIB_TYPE}" ${fmidi_SOURCES})
target_include_directories(fmidi
  PUBLIC sources)
target_compile_definitions(fmidi
  PRIVATE "FMIDI_BUILD=1")
target_link_libraries(fmidi
  PRIVATE fmidi-fmt)
set_target_properties(fmidi PROPERTIES
  CXX_VISIBILITY_PRESET "hidden"
  SOVERSION 0.1)
if(FMIDI_PIC)
  set_target_properties(fmidi PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
endif()
install(TARGETS fmidi
  RUNTIME DESTINATION "bin"
  LIBRARY DESTINATION "lib"
  ARCHIVE DESTINATION "lib")
install(FILES sources/fmidi/fmidi.h
  DESTINATION "include")

###################
# PKGCONFIG FILES #
###################

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/fmidi.pc"
  CONTENT "prefix=${CMAKE_INSTALL_PREFIX}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: fmidi
Description: A MIDI file input library
Version: ${PROJECT_VERSION}
Cflags: -I\${includedir}
Libs: -L\${libdir} -lfmidi
")
install(FILES "${CMAKE_BINARY_DIR}/fmidi.pc"
  DESTINATION "lib/pkgconfig")

############
# PROGRAMS #
############

add_executable(fmidi-read programs/midi-read.cc)
target_link_libraries(fmidi-read PRIVATE fmidi fmidi-fmt)
install(TARGETS fmidi-read
  RUNTIME DESTINATION "bin")

add_executable(fmidi-seq programs/midi-seq.cc)
target_link_libraries(fmidi-seq PRIVATE fmidi fmidi-fmt)
install(TARGETS fmidi-seq
  RUNTIME DESTINATION "bin")

add_executable(fmidi-convert programs/midi-convert.cc)
target_link_libraries(fmidi-convert PRIVATE fmidi fmidi-fmt)
install(TARGETS fmidi-convert
  RUNTIME DESTINATION "bin")

if(fmidi-play_BUILD)
  add_executable(fmidi-play programs/midi-play.cc programs/playlist.cc)
  target_link_libraries(fmidi-play
  PRIVATE fmidi ${rtmidi_LIBRARIES} ${CURSES_LIBRARIES} ${ev_LIBRARY})
  target_include_directories(fmidi-play
    PRIVATE ${rtmidi_INCLUDE_DIRS} ${CURSES_INCLUDE_DIRS})
  link_directories(${rtmidi_LIBRARY_DIRS})
  install(TARGETS fmidi-play
    RUNTIME DESTINATION "bin")
  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(fmidi-play PRIVATE "FMIDI_PLAY_USE_BOOST_FILESYSTEM=")
    target_link_libraries(fmidi-play PRIVATE "${Boost_FILESYSTEM_LIBRARY}" "${Boost_SYSTEM_LIBRARY}")
  endif()
endif()

if(fmidi-grep_BUILD)
  add_executable(fmidi-grep programs/midi-grep.cc)
  target_link_libraries(fmidi-grep PRIVATE fmidi fmidi-fmt)
  install(TARGETS fmidi-grep
    RUNTIME DESTINATION "bin")
endif()
