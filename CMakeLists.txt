cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)
project(amcap LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "AMCap requires Windows, MSVC, the Windows SDK, and the ATL component.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The original AMCap project targets Win32. Configure with -A Win32.")
endif()
enable_language(RC)

option(AMCAP_STATIC_RUNTIME "Link the MSVC runtime statically (/MT or /MTd)" ON)
if(AMCAP_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# The original pre-build batch creates rev.h. Generate it in the build directory instead.
set(AMCAP_REVISION "unknown")
set(AMCAP_REVISION_DATE "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" log -1 --format=%H
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _amcap_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        RESULT_VARIABLE _amcap_revision_result)
    execute_process(COMMAND "${GIT_EXECUTABLE}" log -1 --format=%cI
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _amcap_date OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
        RESULT_VARIABLE _amcap_date_result)
    if(_amcap_revision_result EQUAL 0 AND _amcap_date_result EQUAL 0)
        set(AMCAP_REVISION "${_amcap_revision}")
        set(AMCAP_REVISION_DATE "${_amcap_date}")
    endif()
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/rev.h"
    "#pragma once\n#define VER \"${AMCAP_REVISION}\"\n#define VER_DATE \"${AMCAP_REVISION_DATE}\"\n")

add_library(amcap_baseclasses STATIC
    BaseClasses/amextra.cpp
    BaseClasses/amfilter.cpp
    BaseClasses/amvideo.cpp
    BaseClasses/combase.cpp
    BaseClasses/cprop.cpp
    BaseClasses/ctlutil.cpp
    BaseClasses/ddmm.cpp
    BaseClasses/dllentry.cpp
    BaseClasses/dllsetup.cpp
    BaseClasses/mtype.cpp
    BaseClasses/outputq.cpp
    BaseClasses/pstream.cpp
    BaseClasses/pullpin.cpp
    BaseClasses/refclock.cpp
    BaseClasses/renbase.cpp
    BaseClasses/schedule.cpp
    BaseClasses/seekpt.cpp
    BaseClasses/source.cpp
    BaseClasses/strmctl.cpp
    BaseClasses/sysclock.cpp
    BaseClasses/transfrm.cpp
    BaseClasses/transip.cpp
    BaseClasses/videoctl.cpp
    BaseClasses/vtrans.cpp
    BaseClasses/winctrl.cpp
    BaseClasses/winutil.cpp
    BaseClasses/wxdebug.cpp
    BaseClasses/wxlist.cpp
    BaseClasses/wxutil.cpp)
target_include_directories(amcap_baseclasses PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/BaseClasses")
target_compile_definitions(amcap_baseclasses PRIVATE
    _LIB _WIN32_DCOM FILTER_DLL WINVER=0x06010000 UNICODE _UNICODE
    $<$<CONFIG:Debug>:DEBUG>)
target_compile_options(amcap_baseclasses PRIVATE /Gz)
target_precompile_headers(amcap_baseclasses PRIVATE <streams.h>)

add_executable(amcap WIN32
    SampleCGB.cpp
    amcap.cpp
    crossbar.cpp
    status.cpp
    StdAfx.cpp
    amcap.rc)
target_include_directories(amcap PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(amcap PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
target_compile_definitions(amcap PRIVATE
    _WINDOWS WINVER=0x06010000 _WIN32_WINNT=0x06010000 UNICODE _UNICODE
    $<$<CONFIG:Debug>:DEBUG>)
target_link_libraries(amcap PRIVATE
    amcap_baseclasses quartz winmm msacm32 olepro32 strmiids)
install(TARGETS amcap RUNTIME DESTINATION .)
