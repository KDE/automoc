project(Automoc4)

cmake_minimum_required(VERSION 2.4.5 FATAL_ERROR)

find_package(Qt4 REQUIRED)

include_directories(${QT_INCLUDE_DIR})
add_executable(automoc4 kde4automoc.cpp)

set_target_properties(automoc4  PROPERTIES  SKIP_BUILD_RPATH            FALSE
                                            INSTALL_RPATH_USE_LINK_PATH TRUE )

target_link_libraries(automoc4 ${QT_QTCORE_LIBRARY})

install(TARGETS automoc4 DESTINATION bin)
install(FILES Automoc4Config.cmake automoc4.files.in  DESTINATION  lib/automoc4)
