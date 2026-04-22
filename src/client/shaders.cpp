#include "shaders.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "glm/gtc/type_ptr.hpp"
#include "util.h"

static std::string readFile(const std::filesystem::path& path) {
  std::ifstream f(path);
  if (!f) {
    fprintf(stderr, "Failed to open shader: %s\n", path.c_str());
    exit(EXIT_FAILURE);
  }
  std::stringstream buf;
  buf << f.rdbuf();
  return buf.str();
}

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
  auto base = exeDir();
  std::string vertexShaderText = readFile(base / vertexPath);
  std::string fragmentShaderText = readFile(base / fragmentPath);

  GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
  GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);

  GLint Result = GL_FALSE;
  int InfoLogLength;

  // Compile Vertex Shader
  char const* VertexSourcePointer = vertexShaderText.c_str();
  glShaderSource(vertexShaderId, 1, &VertexSourcePointer, nullptr);
  glCompileShader(vertexShaderId);

  glGetShaderiv(vertexShaderId, GL_COMPILE_STATUS, &Result);
  glGetShaderiv(vertexShaderId, GL_INFO_LOG_LENGTH, &InfoLogLength);
  if (InfoLogLength > 0) {
    std::vector<char> vertexErrorMessage(InfoLogLength + 1);
    glGetShaderInfoLog(vertexShaderId, InfoLogLength, nullptr,
                       &vertexErrorMessage[0]);
    printf("%s\n", &vertexErrorMessage[0]);
  }

  // Compile Fragment Shader
  char const* FragmentSourcePointer = fragmentShaderText.c_str();
  glShaderSource(fragmentShaderId, 1, &FragmentSourcePointer, nullptr);
  glCompileShader(fragmentShaderId);

  glGetShaderiv(fragmentShaderId, GL_COMPILE_STATUS, &Result);
  glGetShaderiv(fragmentShaderId, GL_INFO_LOG_LENGTH, &InfoLogLength);
  if (InfoLogLength > 0) {
    std::vector<char> FragmentShaderErrorMessage(InfoLogLength + 1);
    glGetShaderInfoLog(fragmentShaderId, InfoLogLength, nullptr,
                       &FragmentShaderErrorMessage[0]);
    printf("%s\n", &FragmentShaderErrorMessage[0]);
  }

  // Link the program
  m_id = glCreateProgram();
  glAttachShader(m_id, vertexShaderId);
  glAttachShader(m_id, fragmentShaderId);
  glLinkProgram(m_id);

  glGetProgramiv(m_id, GL_LINK_STATUS, &Result);
  glGetProgramiv(m_id, GL_INFO_LOG_LENGTH, &InfoLogLength);
  if (InfoLogLength > 0) {
    std::vector<char> ProgramErrorMessage(InfoLogLength + 1);
    glGetProgramInfoLog(m_id, InfoLogLength, nullptr, &ProgramErrorMessage[0]);
    printf("%s\n", &ProgramErrorMessage[0]);
  }

  glDetachShader(m_id, vertexShaderId);
  glDetachShader(m_id, fragmentShaderId);

  glDeleteShader(vertexShaderId);
  glDeleteShader(fragmentShaderId);
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

void Shader::destroy() {
  if (m_id) {
    glDeleteProgram(m_id);
    m_id = 0;
  }
  m_locationCache.clear();
}

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
