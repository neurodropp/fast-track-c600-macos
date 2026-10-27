cmake_minimum_required(VERSION 3.20)
project(FastTrackC600 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(external/libASPL)

add_library(FastTrackC600 MODULE
    driver/Driver.cpp
    driver/UsbEngine.cpp)
target_link_libraries(FastTrackC600 PRIVATE libASPL "-framework IOKit" "-framework CoreAudio")
set_target_properties(FastTrackC600 PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION driver
    PREFIX ""
    SUFFIX ""
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/driver/Info.plist.in")

add_custom_command(TARGET FastTrackC600 POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:FastTrackC600>"
    COMMENT "Ad-hoc signing development driver")

add_executable(c600-engine-check tools/engine_check.cpp driver/UsbEngine.cpp)
target_link_libraries(c600-engine-check PRIVATE "-framework IOKit" "-framework CoreFoundation")

add_executable(c600-plugin-check tools/plugin_check.cpp)
target_link_libraries(c600-plugin-check PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
