// Link-only smoke test: exercises the client's full library graph without
// requiring a display, GL context, or network. Forces static-archive member
// pull-in by taking the address of one symbol from each linked library.

// clang-format off
#include <glad/gl.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <assimp/version.h>
#include <enet/enet.h>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

int main() {
    volatile void* sinks[] = {
        reinterpret_cast<void*>(&glfwInit),
        reinterpret_cast<void*>(&glfwTerminate),
        reinterpret_cast<void*>(&enet_initialize),
        reinterpret_cast<void*>(&enet_deinitialize),
        reinterpret_cast<void*>(&aiGetVersionMajor),
        reinterpret_cast<void*>(&gladLoadGL),
        reinterpret_cast<void*>(&ImGui::GetVersion),
    };
    (void)sinks;

    glm::vec3 v{1.0f, 2.0f, 3.0f};
    entt::registry r;
    (void)r.create();
    (void)v;

    return 0;
}
