#pragma once

#include <glad/gl.h>

#include <string>

GLuint loadShaders(const std::string& vertexPath,
                   const std::string& fragmentPath);
