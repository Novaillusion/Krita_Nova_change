# Unit tests for patch 08 (settings export/import), built against the patched
# Krita source tree: cmake -DKRITA_SRC=/path/to/patched/krita ..
cmake_minimum_required(VERSION 3.16)
project(krita_settings_transfer_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC OFF)

find_package(Qt5 REQUIRED COMPONENTS Core)
find_package(KF5Config REQUIRED)

if (NOT KRITA_SRC)
    message(FATAL_ERROR "Pass -DKRITA_SRC=<patched krita source>")
endif()

add_executable(test_settings_transfer
    test_settings_transfer.cpp
    ${KRITA_SRC}/libs/ui/KisSettingsTransferCore.cpp
)
target_include_directories(test_settings_transfer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
    ${KRITA_SRC}/libs/ui
)
target_link_libraries(test_settings_transfer PRIVATE Qt5::Core KF5::ConfigCore)

enable_testing()
add_test(NAME settings_transfer COMMAND test_settings_transfer)
