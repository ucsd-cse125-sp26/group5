cmake_minimum_required(VERSION 4.1)
project(cse125-game)

set(CMAKE_CXX_STANDARD 20)

add_library(shared
    src/shared/hello.cpp)
target_include_directories(shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(lib/glfw)

add_subdirectory(lib/enet)

add_library(zpp_bits INTERFACE)
target_include_directories(zpp_bits INTERFACE lib/zpp_bits)

target_link_libraries(shared PUBLIC zpp_bits)

# Path to glad directory
set(GLAD_SOURCES_DIR "${PROJECT_SOURCE_DIR}/lib/glad/")
# Path to glad cmake files
add_subdirectory("${GLAD_SOURCES_DIR}/cmake" glad_cmake)
# Specify glad settings
glad_add_library(glad_gl_core_46 REPRODUCIBLE API gl:core=4.6)

add_subdirectory(lib/entt)

add_library(imgui
    lib/imgui/imgui.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/backends/imgui_impl_glfw.cpp
    lib/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    lib/imgui
    lib/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw glad_gl_core_46)

add_executable(client)
target_sources(client PRIVATE
    src/client/main.cpp
    src/client/client_network.cpp
    src/client/client_game.cpp
)
target_sources(client PRIVATE src/client/main.cpp)
target_include_directories(client PRIVATE lib/enet/include)
target_link_libraries(client PRIVATE shared glfw enet glad_gl_core_46 imgui EnTT::EnTT)

add_executable(server)
target_sources(server PRIVATE
    src/server/main.cpp
    src/server/server_network.cpp
    src/server/server_game.cpp
)
target_sources(server PRIVATE src/server/main.cpp)
target_include_directories(server PRIVATE lib/enet/include)
target_link_libraries(server PRIVATE shared enet EnTT::EnTT)

include(CTest)
if(BUILD_TESTING)
    # GoogleTest lives in lib/googletest (git submodule). Pin version with:
    #   cd lib/googletest && git checkout v1.14.0
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(lib/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
