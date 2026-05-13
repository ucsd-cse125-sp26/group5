// clang-format off
#include <glad/gl.h>
// clang-format on

#include "renderer2d.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "glm/ext/matrix_clip_space.hpp"

bool Renderer2D::init() {
  uint32_t white = 0xFFFFFFFF;
  glGenTextures(1, &whiteTexture_);
  glBindTexture(GL_TEXTURE_2D, whiteTexture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               &white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &ebo_);

  glBindVertexArray(vao_);

  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(kMaxVertices * sizeof(Vertex2D)),
               nullptr, GL_DYNAMIC_DRAW);

  std::vector<uint16_t> indices(kMaxIndices);
  for (int i = 0; i < kMaxQuads; i++) {
    auto base = static_cast<uint16_t>(i * 4);
    int idx = i * 6;
    indices[idx + 0] = base + 0;
    indices[idx + 1] = base + 1;
    indices[idx + 2] = base + 2;
    indices[idx + 3] = base + 2;
    indices[idx + 4] = base + 3;
    indices[idx + 5] = base + 0;
  }
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(indices.size() * sizeof(uint16_t)),
               indices.data(), GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                        reinterpret_cast<void*>(offsetof(Vertex2D, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                        reinterpret_cast<void*>(offsetof(Vertex2D, u)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                        reinterpret_cast<void*>(offsetof(Vertex2D, r)));

  glBindVertexArray(0);

  vertices_.reserve(kMaxVertices);

  shader_.emplace("shaders/vertex_2d.glsl", "shaders/fragment_2d.glsl");
  if (!shader_ || !shader_->valid()) {
    fprintf(stderr, "Renderer2D: failed to compile 2D shaders\n");
    return false;
  }

  return true;
}

void Renderer2D::shutdown() {
  if (ebo_) {
    glDeleteBuffers(1, &ebo_);
    ebo_ = 0;
  }
  if (vbo_) {
    glDeleteBuffers(1, &vbo_);
    vbo_ = 0;
  }
  if (vao_) {
    glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
  }
  if (whiteTexture_) {
    glDeleteTextures(1, &whiteTexture_);
    whiteTexture_ = 0;
  }
  shader_.reset();
}

bool Renderer2D::reloadShaders() {
  Shader candidate("shaders/vertex_2d.glsl", "shaders/fragment_2d.glsl");
  if (candidate.valid()) {
    shader_.emplace(std::move(candidate));
    printf("Reloaded: shaders/vertex_2d.glsl + shaders/fragment_2d.glsl\n");
    return true;
  }
  fprintf(stderr,
          "Renderer2D: shader reload failed, keeping previous program\n");
  return false;
}

void Renderer2D::begin(float logicalWidth, float logicalHeight) {
  projection_ =
      glm::ortho(0.0f, logicalWidth, 0.0f, logicalHeight, -1.0f, 1.0f);
  vertices_.clear();
  quadCount_ = 0;
  boundTexture_ = 0;
}

void Renderer2D::flush() {
  if (quadCount_ == 0) return;
  if (!shader_ || !shader_->valid()) return;

  shader_->use();
  shader_->setMat4("uProjection", projection_);
  shader_->setInt("uTexture", 0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, boundTexture_);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferSubData(GL_ARRAY_BUFFER, 0,
                  static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex2D)),
                  vertices_.data());
  glDrawElements(GL_TRIANGLES, quadCount_ * 6, GL_UNSIGNED_SHORT, nullptr);
  glBindVertexArray(0);

  vertices_.clear();
  quadCount_ = 0;
}

void Renderer2D::pushQuad(float x, float y, float w, float h, float u0,
                          float v0, float u1, float v1, const glm::vec4& color,
                          GLuint texture) {
  if (texture != boundTexture_ && quadCount_ > 0) {
    flush();
  }
  if (quadCount_ >= kMaxQuads) {
    flush();
  }
  boundTexture_ = texture;

  Vertex2D bl = {.x = x,
                 .y = y,
                 .u = u0,
                 .v = v0,
                 .r = color.r,
                 .g = color.g,
                 .b = color.b,
                 .a = color.a};
  Vertex2D br = {.x = x + w,
                 .y = y,
                 .u = u1,
                 .v = v0,
                 .r = color.r,
                 .g = color.g,
                 .b = color.b,
                 .a = color.a};
  Vertex2D tr = {.x = x + w,
                 .y = y + h,
                 .u = u1,
                 .v = v1,
                 .r = color.r,
                 .g = color.g,
                 .b = color.b,
                 .a = color.a};
  Vertex2D tl = {.x = x,
                 .y = y + h,
                 .u = u0,
                 .v = v1,
                 .r = color.r,
                 .g = color.g,
                 .b = color.b,
                 .a = color.a};

  vertices_.push_back(bl);
  vertices_.push_back(br);
  vertices_.push_back(tr);
  vertices_.push_back(tl);
  quadCount_++;
}

void Renderer2D::drawRect(float x, float y, float w, float h, glm::vec4 color) {
  pushQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color, whiteTexture_);
}

void Renderer2D::drawTexturedRect(float x, float y, float w, float h,
                                  GLuint texture, glm::vec4 tint) {
  pushQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, tint, texture);
}

void Renderer2D::drawSubRect(float x, float y, float w, float h, GLuint texture,
                             glm::vec4 uvRect, glm::vec4 tint) {
  pushQuad(x, y, w, h, uvRect.x, uvRect.y, uvRect.z, uvRect.w, tint, texture);
}

void Renderer2D::end() { flush(); }
