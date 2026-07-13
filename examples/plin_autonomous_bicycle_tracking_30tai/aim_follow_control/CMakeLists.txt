cmake_minimum_required(VERSION 3.16)
project(aim_follow_control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(aim_follow_control
    src/aim_follow_controller.cpp
)

target_include_directories(aim_follow_control
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(aim_follow_controller_test
    test/aim_follow_controller_test.cpp
)

target_link_libraries(aim_follow_controller_test
    PRIVATE
    aim_follow_control
)

add_executable(aim_follow_synthetic_board_test
    test/aim_follow_synthetic_board_test.cpp
)

target_link_libraries(aim_follow_synthetic_board_test
    PRIVATE
    aim_follow_control
)
