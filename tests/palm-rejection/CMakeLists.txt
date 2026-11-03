# Unit tests for patch 09 (palm rejection), built against the patched Krita
# source tree: cmake -DKRITA_SRC=/path/to/patched/krita ..
cmake_minimum_required(VERSION 3.16)
project(krita_palm_rejection_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Test)

if (NOT KRITA_SRC)
    message(FATAL_ERROR "Pass -DKRITA_SRC=<patched krita source>")
endif()

add_executable(test_palm_rejection
    test_palm_rejection.cpp
    stub/kis_config_notifier.cpp
    stub/kis_config_notifier.h
    ${KRITA_SRC}/libs/ui/input/KisPalmRejection.cpp
    ${KRITA_SRC}/libs/ui/input/KisPalmRejection.h
)
target_include_directories(test_palm_rejection PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stub
    ${KRITA_SRC}/libs/ui/input
)
target_link_libraries(test_palm_rejection PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test)

enable_testing()
add_test(NAME palm_rejection COMMAND test_palm_rejection)
set_tests_properties(palm_rejection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
