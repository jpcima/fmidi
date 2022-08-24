cmake_minimum_required(VERSION 3.3)
include(CMakeDependentOption)

project(fmidi VERSION 0.1.1 LANGUAGES C CXX)

set(cmake_MODULE_DIR "${PROJECT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${cmake_MODULE_DIR}")

option(FMIDI_ENABLE_DEBUG "enable debugging features" OFF)
option(FMIDI_PIC "enable position independent code" ON)
option(FMIDI_STATIC "build as static library" ON)
cmake_dependent_option(FMIDI_PROGRAMS "build the programs" ON
  "CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR" OFF)

include(ExtraCompilerFlags)
enable_gcc_warning(all)
enable_colors()

set(CMAKE_CXX_STANDARD 11)
if(FMIDI_PROGRAMS)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if(CMAKE_SYSTEM_NAME MATCHES Windows)
  add_definitions("-D_CRT_SECURE_NO_WARNINGS=1")
endif()

#####################
# FEATURE DETECTION #
#####################

if(FMIDI_PROGRAMS)
  set(fmidi-play_BUILD FALSE)
  find_library(ev_LIBRARY ev)

  find_package(Curses)
  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(rtmidi "rtmidi")
    if(rtmidi_FOUND)
      link_directories(${rtmidi_LIBRARY_DIRS})
    endif()
  endif()
  if(NOT PKG_CONFIG_FOUND)
    message(STATUS "pkg-config is missing, NOT building program fmidi-play.")
  elseif(NOT rtmidi_FOUND)
    message(STATUS "rtmidi is missing, NOT building program fmidi-play.")
  elseif(NOT CURSES_FOUND)
    message(STATUS "curses is missing, NOT building program fmidi-play.")
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
  else()
    set(fmidi-grep_BUILD TRUE)
  endif()
endif()

##############
# THIRDPARTY #
##############

if(TARGET fmidi-fmt)
  message(STATUS "Target fmidi-fmt exists, using it as library")
else()
  message(STATUS "Target fmidi-fmt does not exist, using the system library")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(fmt "fmt" REQUIRED)
  add_library(fmidi-fmt INTERFACE)
  target_include_directories(fmidi-fmt INTERFACE ${fmt_INCLUDE_DIRS})
  target_link_libraries(fmidi-fmt INTERFACE ${fmt_LIBRARIES})
endif()

#############
# LIBRARIES #
#############

set(fmidi_SOURCES
  sources/fmidi/u_memstream.cc
  sources/fmidi/u_stdio.cc
  sources/fmidi/u_iterator.cc
  sources/fmidi/file/read_smf.cc
  sources/fmidi/file/write_smf.cc
  sources/fmidi/file/read_xmi.cc
  sources/fmidi/file/read_mus.cc
  sources/fmidi/file/identify.cc
  sources/fmidi/fmidi_internal.cc
  sources/fmidi/fmidi_seq.cc
  sources/fmidi/fmidi_util.cc
  sources/fmidi/fmidi_player.cc)

if(FMIDI_STATIC)
  add_library(fmidi STATIC ${fmidi_SOURCES})
else()
  add_library(fmidi SHARED ${fmidi_SOURCES})
endif()
target_include_directories(fmidi PUBLIC sources)
target_compile_definitions(fmidi PRIVATE "FMIDI_BUILD=1")
if(FMIDI_STATIC)
  target_compile_definitions(fmidi PUBLIC "FMIDI_STATIC=1")
endif()
if(FMIDI_ENABLE_DEBUG)
  target_compile_definitions(fmidi PUBLIC "-DFMIDI_DEBUG=1")
endif()
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

if(FMIDI_PROGRAMS)
  add_executable(fmidi-read programs/midi-read.cc)
  target_link_libraries(fmidi-read PRIVATE fmidi fmidi-fmt)
  install(TARGETS fmidi-read
    RUNTIME DESTINATION "bin")
  install(FILES "man/fmidi-read.1"
    DESTINATION "share/man/man1")

  add_executable(fmidi-seq programs/midi-seq.cc)
  target_link_libraries(fmidi-seq PRIVATE fmidi fmidi-fmt)
  install(TARGETS fmidi-seq
    RUNTIME DESTINATION "bin")
  install(FILES "man/fmidi-seq.1"
    DESTINATION "share/man/man1")

  add_executable(fmidi-convert programs/midi-convert.cc)
  target_link_libraries(fmidi-convert PRIVATE fmidi fmidi-fmt)
  install(TARGETS fmidi-convert
    RUNTIME DESTINATION "bin")
  install(FILES "man/fmidi-convert.1"
    DESTINATION "share/man/man1")

  if(fmidi-play_BUILD)
    add_executable(fmidi-play programs/midi-play.cc programs/playlist.cc)
    target_link_libraries(fmidi-play
      PRIVATE fmidi ${rtmidi_LIBRARIES} ${CURSES_LIBRARIES} ${ev_LIBRARY})
    target_include_directories(fmidi-play
      PRIVATE ${rtmidi_INCLUDE_DIRS} ${CURSES_INCLUDE_DIRS})
    install(TARGETS fmidi-play
      RUNTIME DESTINATION "bin")
    install(FILES "man/fmidi-play.1"
      DESTINATION "share/man/man1")
    if(fmidi_HAVE_FTS)
      target_compile_definitions(fmidi-play PRIVATE "FMIDI_PLAY_HAVE_FTS")
    endif()
  endif()

  if(fmidi-grep_BUILD)
    add_executable(fmidi-grep programs/midi-grep.cc)
    target_link_libraries(fmidi-grep PRIVATE fmidi fmidi-fmt)
    install(TARGETS fmidi-grep
      RUNTIME DESTINATION "bin")
    install(FILES "man/fmidi-grep.1"
      DESTINATION "share/man/man1")
  endif()
endif()
