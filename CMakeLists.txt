cmake_minimum_required(VERSION 4.1)
project(cse125-game)

set(CMAKE_CXX_STANDARD 17)

add_library(shared
    src/shared/hello.cpp)
target_include_directories(shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(lib/glfw)
find_package(OpenGL REQUIRED)

add_subdirectory(lib/enet)

add_executable(client)
target_sources(client PRIVATE src/client/main.cpp)
target_link_libraries(client PRIVATE shared glfw OpenGL::GL enet)

add_executable(server)
target_sources(server PRIVATE src/server/main.cpp)
target_link_libraries(server PRIVATE shared enet)
