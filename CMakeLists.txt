cmake_minimum_required(VERSION 4.1)
project(cse125-game)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(ENABLE_PROFILING "Enable Simple Profiler" OFF)
if(ENABLE_PROFILING)
    add_compile_definitions(ENABLE_PROFILING)
endif()

if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
# set(CMAKE_CXX_CLANG_TIDY clang-tidy)

add_library(shared
    src/shared/hello.cpp
    src/shared/util.cpp
    )
target_include_directories(shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_subdirectory(lib/glfw)

add_subdirectory(lib/glm)

add_subdirectory(lib/enet)
target_include_directories(enet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/enet/include)

if(WIN32)
    target_link_libraries(enet ws2_32 winmm)
endif()

add_library(zpp_bits INTERFACE)
target_include_directories(zpp_bits INTERFACE lib/zpp_bits)

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE lib/stb)

target_link_libraries(shared PUBLIC zpp_bits)

# Path to glad directory
set(GLAD_SOURCES_DIR "${PROJECT_SOURCE_DIR}/lib/glad/")
# Path to glad cmake files
add_subdirectory("${GLAD_SOURCES_DIR}/cmake" glad_cmake)
# Specify glad settings
glad_add_library(glad_gl_core_41 REPRODUCIBLE API gl:core=4.1)

add_subdirectory(lib/entt)

if(NOT APPLE)
  SET(ASSIMP_BUILD_ZLIB ON CACHE BOOL "build zlib")
endif()
SET(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
SET(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/assimp)
set(PHYSICS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/lib/jolt)
add_subdirectory(lib/jolt/Build)
if(MSVC)
  set_target_properties(Jolt PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
  )
endif()
if(NOT MSVC)
  target_compile_options(Jolt PRIVATE -Wno-array-bounds)
endif()

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
target_link_libraries(imgui PUBLIC glfw glad_gl_core_41)

add_executable(client)
target_sources(client PRIVATE
    src/client/main.cpp
    src/client/client_network.cpp
    src/client/client_game.cpp
    src/client/client_graphics.cpp
    src/client/shaders.cpp
    src/client/asset.cpp
)
target_link_libraries(client PRIVATE shared glfw enet glad_gl_core_41 imgui EnTT::EnTT glm::glm assimp stb_image Jolt)

add_custom_command(TARGET client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/client/shaders
        $<TARGET_FILE_DIR:client>/shaders
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    add_custom_command(TARGET client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:client>/assets
    )
endif()

# this is for testing purposes
add_library(server_lib STATIC
    src/server/server_network.cpp
    src/server/server_game.cpp
    src/server/physics_engine.cpp
    src/server/scene.cpp
    src/server/server_level_loader.cpp
)
target_include_directories(server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(server_lib PUBLIC shared enet EnTT::EnTT glm::glm Jolt assimp)
target_include_directories(server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/jolt/Build/..)

add_executable(server)
target_sources(server PRIVATE src/server/main.cpp)
target_link_libraries(server PRIVATE server_lib)

add_executable(client_link_smoke src/client/link_smoke.cpp)
target_link_libraries(client_link_smoke PRIVATE
    shared glfw enet glad_gl_core_41 imgui EnTT::EnTT glm::glm assimp stb_image
)

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    # GoogleTest lives in lib/googletest (git submodule). Pin version with:
    #   cd lib/googletest && git checkout v1.14.0
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(lib/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
    add_test(NAME client_link_smoke COMMAND client_link_smoke)
endif()
