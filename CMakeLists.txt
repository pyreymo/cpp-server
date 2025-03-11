cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED true)

project(event_app VERSION 0.1.0 LANGUAGES C CXX)

add_executable(event_app main.cpp)
target_link_libraries(event_app PRIVATE uv)

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
