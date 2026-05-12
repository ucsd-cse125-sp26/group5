#pragma once

#include <glad/gl.h>

#include <optional>
#include <vector>

#include "client/shaders.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float4.hpp"

struct Vertex2D {
  float x, y;
  float u, v;
  float r, g, b, a;
};

class Renderer2D {
 public:
  static constexpr int kMaxQuads = 4096;
  static constexpr int kMaxVertices = kMaxQuads * 4;
  static constexpr int kMaxIndices = kMaxQuads * 6;

  bool init();
  void shutdown();
  bool reloadShaders();

  void begin(float logicalWidth, float logicalHeight);
  void drawRect(float x, float y, float w, float h, glm::vec4 color);
  void drawTexturedRect(float x, float y, float w, float h, GLuint texture,
                        glm::vec4 tint = glm::vec4(1.0f));
  void drawSubRect(float x, float y, float w, float h, GLuint texture,
                   glm::vec4 uvRect, glm::vec4 tint = glm::vec4(1.0f));
  void end();

 private:
  void flush();
  void pushQuad(float x, float y, float w, float h, float u0, float v0,
                float u1, float v1, const glm::vec4& color, GLuint texture);

  GLuint vao_ = 0;
  GLuint vbo_ = 0;
  GLuint ebo_ = 0;
  GLuint whiteTexture_ = 0;
  std::optional<Shader> shader_;

  glm::mat4 projection_{1.0f};
  std::vector<Vertex2D> vertices_;
  int quadCount_ = 0;
  GLuint boundTexture_ = 0;
};
