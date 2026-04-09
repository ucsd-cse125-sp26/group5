#include <enet/enet.h>
#include <entt/entt.hpp>
#include <vector>
#include <cstdint>
#include <cstring>
#include "shared/protocol.h"
#include "shared/components.h"
#include "shared/component_registry.h"

void broadcastState(ENetHost* server, entt::registry& registry);