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

# Path to glad directory
set(GLAD_SOURCES_DIR "${PROJECT_SOURCE_DIR}/lib/glad/")
# Path to glad cmake files
add_subdirectory("${GLAD_SOURCES_DIR}/cmake" glad_cmake)
# Specify glad settings
glad_add_library(glad_gl_core_46 REPRODUCIBLE API gl:core=4.6)

add_executable(client)
target_sources(client PRIVATE src/client/main.cpp)
target_link_libraries(client PRIVATE shared glfw OpenGL::GL enet glad_gl_core_46)

add_executable(server)
target_sources(server PRIVATE src/server/main.cpp)
target_link_libraries(server PRIVATE shared enet)
