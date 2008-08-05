project(Automoc4)

cmake_minimum_required(VERSION 2.4.5 FATAL_ERROR)

find_package(Qt4 REQUIRED)

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_C_COMPILER MATCHES "icc")
   set ( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnon-virtual-dtor -Wno-long-long -ansi -Wundef -Wcast-align -Wchar-subscripts -Wall -W -Wpointer-arith -Wformat-security -fno-check-new -fno-common")
endif (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_C_COMPILER MATCHES "icc")

# set the current version number
include(Automoc4Version.cmake)

configure_file(automoc4_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/automoc4_config.h)

# Always include srcdir and builddir in include path
set(CMAKE_INCLUDE_CURRENT_DIR ON)
include_directories(${QT_INCLUDE_DIR})
add_executable(automoc4 kde4automoc.cpp)

set_target_properties(automoc4  PROPERTIES  SKIP_BUILD_RPATH            FALSE
                                            INSTALL_RPATH_USE_LINK_PATH TRUE )

target_link_libraries(automoc4 ${QT_QTCORE_LIBRARY})

install(TARGETS automoc4 DESTINATION bin)
install(FILES Automoc4Config.cmake Automoc4Version.cmake automoc4.files.in  DESTINATION  lib${LIB_SUFFIX}/automoc4)
