#pragma once

#include <unordered_map>

#include "asset.h"
#include "client/client_game.h"
#include "glad/gl.h"

void initPointLights(GLuint shaderProgram);
bool setupCameraMatrix(GLuint shaderProgram, const ClientGame& game);
void updateDirectionalLight(GLuint shaderProgram, const ClientGame& game);
void updatePointLights(GLuint shaderProgram, const ClientGame& game);
void renderEntities(GLuint shaderProgram, ClientGame& game,
                    std::unordered_map<std::string, Model*>& models);
