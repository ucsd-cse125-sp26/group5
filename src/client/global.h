#ifndef GLOBALS_H
#define GLOBALS_H

#include <entt/entt.hpp>
#include <map>
#include <cstdint>

inline entt::registry registry;
inline std::map<uint32_t, entt::entity> entityMap;
inline uint32_t myEntityId = UINT32_MAX;

#endif