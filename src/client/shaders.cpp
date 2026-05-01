#include "shaders.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "glm/gtc/type_ptr.hpp"
#include "shared/util.h"

static bool readFile(const std::filesystem::path& path, std::string& out) {
  std::ifstream f(path);
  if (!f) {
    fprintf(stderr, "Failed to open shader: %s\n", path.c_str());
    return false;
  }
  std::stringstream buf;
  buf << f.rdbuf();
  out = buf.str();
  return true;
}

static bool compileStage(GLuint shaderId, const char* source, const char* tag) {
  glShaderSource(shaderId, 1, &source, nullptr);
  glCompileShader(shaderId);
  GLint result = GL_FALSE;
  GLint logLen = 0;
  glGetShaderiv(shaderId, GL_COMPILE_STATUS, &result);
  glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLen);
  if (logLen > 0) {
    std::vector<char> msg(logLen + 1);
    glGetShaderInfoLog(shaderId, logLen, nullptr, msg.data());
    fprintf(stderr, "[%s] %s\n", tag, msg.data());
  }
  return result == GL_TRUE;
}

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
  auto base = exeDir();
  std::string vertexShaderText, fragmentShaderText;
  if (!readFile(base / vertexPath, vertexShaderText) ||
      !readFile(base / fragmentPath, fragmentShaderText)) {
    return;
  }

  GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
  GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);

  bool vsOk = compileStage(vertexShaderId, vertexShaderText.c_str(),
                           vertexPath.c_str());
  bool fsOk = compileStage(fragmentShaderId, fragmentShaderText.c_str(),
                           fragmentPath.c_str());

  if (!vsOk || !fsOk) {
    glDeleteShader(vertexShaderId);
    glDeleteShader(fragmentShaderId);
    return;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vertexShaderId);
  glAttachShader(program, fragmentShaderId);
  glLinkProgram(program);

  GLint linkStatus = GL_FALSE;
  GLint logLen = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
  if (logLen > 0) {
    std::vector<char> msg(logLen + 1);
    glGetProgramInfoLog(program, logLen, nullptr, msg.data());
    fprintf(stderr, "[link %s + %s] %s\n", vertexPath.c_str(),
            fragmentPath.c_str(), msg.data());
  }

  glDetachShader(program, vertexShaderId);
  glDetachShader(program, fragmentShaderId);
  glDeleteShader(vertexShaderId);
  glDeleteShader(fragmentShaderId);

  if (linkStatus != GL_TRUE) {
    glDeleteProgram(program);
    return;
  }
  m_id = program;
}

Shader::~Shader() {
  if (m_id) glDeleteProgram(m_id);
}

Shader::Shader(Shader&& other) noexcept
    : m_id(other.m_id), m_locationCache(std::move(other.m_locationCache)) {
  other.m_id = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
  if (this != &other) {
    if (m_id) glDeleteProgram(m_id);
    m_id = other.m_id;
    m_locationCache = std::move(other.m_locationCache);
    other.m_id = 0;
  }
  return *this;
}

void Shader::use() const { glUseProgram(m_id); }

GLuint Shader::id() const { return m_id; }

GLint Shader::getLocation(const std::string& name) const {
  auto it = m_locationCache.find(name);
  if (it != m_locationCache.end()) return it->second;
  GLint loc = glGetUniformLocation(m_id, name.c_str());
  if (loc == -1) {
    fprintf(stderr, "Warning: uniform '%s' not found in shader %u\n",
            name.c_str(), m_id);
  }
  m_locationCache[name] = loc;
  return loc;
}

void Shader::setInt(const std::string& name, int value) const {
  glUniform1i(getLocation(name), value);
}

void Shader::setFloat(const std::string& name, float value) const {
  glUniform1f(getLocation(name), value);
}

void Shader::setVec3(const std::string& name, float x, float y, float z) const {
  glUniform3f(getLocation(name), x, y, z);
}

void Shader::setVec3(const std::string& name, const glm::vec3& v) const {
  glUniform3f(getLocation(name), v.x, v.y, v.z);
}

void Shader::setMat3(const std::string& name, const glm::mat3& m) const {
  glUniformMatrix3fv(getLocation(name), 1, GL_FALSE, glm::value_ptr(m));
}

void Shader::setMat4(const std::string& name, const glm::mat4& m) const {
  glUniformMatrix4fv(getLocation(name), 1, GL_FALSE, glm::value_ptr(m));
}
