# will make it build this weekend, Alex

include_directories(${QT_INCLUDE_DIR})
add_executable(kde4automoc kde4automoc.cpp)
kde4_handle_rpath_for_executable(automoc4 "RUN_UNINSTALLED")

target_link_libraries(automoc4 ${QT_QTCORE_LIBRARY})
install(TARGETS automoc4 DESTINATION bin)
