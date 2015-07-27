cmake_minimum_required(VERSION 2.8.9)
project(Automoc4)

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

# Always include srcdir and builddir in include path
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${QT_INCLUDE_DIR})
add_executable(automoc4 kde4automoc.cpp)

set_target_properties(automoc4  PROPERTIES  SKIP_BUILD_RPATH            FALSE
                                            INSTALL_RPATH_USE_LINK_PATH TRUE )

target_link_libraries(automoc4 ${QT_LIBRARIES})

install(TARGETS automoc4 DESTINATION bin)
install(FILES Automoc4Config.cmake Automoc4Version.cmake automoc4.files.in  DESTINATION  lib${LIB_SUFFIX}/automoc4)
