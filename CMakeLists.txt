project(Automoc4)

cmake_minimum_required(VERSION 2.4.5 FATAL_ERROR)

if(WIN32)
    # make cmake 2.6 happy
    if(COMMAND cmake_policy)
        cmake_policy(SET CMP0003 NEW)
    endif(COMMAND cmake_policy)
endif(WIN32)

include(CTest)

find_package(Qt4 REQUIRED COMPONENTS QtCore )
include(${QT_USE_FILE})

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_C_COMPILER MATCHES "icc")
   set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnon-virtual-dtor -Wno-long-long -ansi -Wundef -Wcast-align -Wchar-subscripts -Wall -W -Wpointer-arith -Wformat-security -fno-check-new -fno-common")
endif (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_C_COMPILER MATCHES "icc")

# set the current version number
include(Automoc4Version.cmake)

# set up packaging
include(Automoc4CPack.cmake)

configure_file(automoc4_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/automoc4_config.h)

if(MSVC AND AUTOMOC_STATIC)
    IF(EXISTS "${QT_MKSPECS_DIR}/default/qmake.conf")
        FILE(READ "${QT_MKSPECS_DIR}/default/qmake.conf" _qmake_FILE_contents)
        STRING(REGEX MATCH "QMAKE_CFLAGS_RELEASE[^\n]+" QMAKE_CFLAGS_RELEASE "${_qmake_FILE_contents}")
    ENDIF(EXISTS "${QT_MKSPECS_DIR}/default/qmake.conf")

    if (QMAKE_CFLAGS_RELEASE MATCHES ".*-MT.*")
        MESSAGE(STATUS "Qt is build in static mode with static runtime - use static build mode with static runtime too")
        set (MSVCRT_COMPILE_FLAGS "-MT /Os /Oy /GL")
        set (MSVCRT_LINK_FLAGS "/NODEFAULTLIB:msvcrt")
    else (QMAKE_CFLAGS_RELEASE MATCHES ".*-MT.*")
        set (MSVCRT_COMPILE_FLAGS "-MD")
        set (MSVCRT_LINK_FLAGS "")
        MESSAGE(STATUS "Qt is build in static mode using shared runtime - use static build mode too")
    endif (QMAKE_CFLAGS_RELEASE MATCHES ".*-MT.*")

    set (CMAKE_C_FLAGS_RELEASE "${MSVCRT_COMPILE_FLAGS} /O2 /Ob2 /D NDEBUG")
    set (CMAKE_CXX_FLAGS_RELEASE "${MSVCRT_COMPILE_FLAGS} /O2 /Ob2 /D NDEBUG")
    set (CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO ${MSVCRT_LINK_FLAGS}")
endif(MSVC AND AUTOMOC_STATIC)

# Create the kwsys library from CMake.
set(KWSYS_NAMESPACE cmsys)
set(KWSYS_USE_SystemTools 1)
set(KWSYS_USE_Directory 1)
set(KWSYS_USE_RegularExpression 1)
set(KWSYS_USE_Base64 0)
set(KWSYS_USE_Glob 1)
set(KWSYS_USE_MD5 0)
set(KWSYS_USE_Process 1)
set(KWSYS_USE_CommandLineArguments 1)
set(KWSYS_HEADER_ROOT ${CMAKE_BINARY_DIR}/Source)

add_subdirectory(kwsys)


# Always include srcdir and builddir in include path
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${QT_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/Source)

add_executable(automoc4 kde4automoc.cpp
                        cmSystemTools.cxx
                        cmStandardIncludes.cxx
                        cmWin32ProcessExecution.cxx
              )

set_target_properties(automoc4  PROPERTIES  SKIP_BUILD_RPATH            FALSE
                                            INSTALL_RPATH_USE_LINK_PATH TRUE )

target_link_libraries(automoc4 ${QT_LIBRARIES} cmsys)

install(TARGETS automoc4 DESTINATION bin)
install(FILES Automoc4Config.cmake Automoc4Version.cmake automoc4.files.in  DESTINATION  lib${LIB_SUFFIX}/automoc4)
