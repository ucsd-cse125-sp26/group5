#pragma once

#include <glad/gl.h>

#include <string>
#include <unordered_map>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"

class Shader {
 public:
  Shader(const std::string& vertexPath, const std::string& fragmentPath);
  ~Shader();
  Shader(Shader&& other) noexcept;
  Shader& operator=(Shader&& other) noexcept;
  Shader(const Shader&) = delete;
  Shader& operator=(const Shader&) = delete;

  void use() const;
  GLuint id() const;

  void setInt(const std::string& name, int value) const;
  void setFloat(const std::string& name, float value) const;
  void setVec3(const std::string& name, float x, float y, float z) const;
  void setVec3(const std::string& name, const glm::vec3& v) const;
  void setMat3(const std::string& name, const glm::mat3& m) const;
  void setMat4(const std::string& name, const glm::mat4& m) const;

 private:
  GLuint m_id = 0;
  mutable std::unordered_map<std::string, GLint> m_locationCache;
  GLint getLocation(const std::string& name) const;
};
