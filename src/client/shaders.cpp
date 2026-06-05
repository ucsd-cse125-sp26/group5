#include "shaders.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "glm/gtc/type_ptr.hpp"
#include "shared/shader_constants.h"
#include "shared/util.h"

// Mirror shader_constants.h as GLSL #defines; injected after #version.
static std::string buildShaderConstantsBlock() {
  std::ostringstream s;
  s << "#define K_MAX_POINT_LIGHTS " << shared::kMaxPointLights << "\n";
  s << "#define K_MAX_LIGHTING_SHADER_LIGHTS "
    << shared::kMaxLightingShaderLights << "\n";
  s << "#define K_POINT_SHADOW_LAYERS " << shared::kPointShadowLayers << "\n";
  s << "#define K_SHADOW_CASCADE_COUNT " << shared::kShadowCascadeCount << "\n";
  s << "#define K_POINT_SHADOW_NEAR " << shared::kPointShadowNear << "\n";
  s << "#define K_POINT_SHADOW_FAR " << shared::kPointShadowFar << "\n";
  s << "#define K_MAX_PALETTE_COLORS " << shared::kMaxPaletteColors << "\n";
  s << "#define K_MAX_BONES " << shared::kMaxBones << "\n";
  s << "#define K_MAX_FOG_BOXES " << shared::kMaxFogBoxes << "\n";
  return s.str();
}

static std::string injectConstants(const std::string& source) {
  static const std::string block = buildShaderConstantsBlock();
  size_t versionEnd = source.find('\n');
  if (versionEnd == std::string::npos) return source;
  return source.substr(0, versionEnd + 1) + block +
         source.substr(versionEnd + 1);
}

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

// Empty geomPath skips the geometry stage. Returns 0 on any failure.
static GLuint linkProgram(const std::string& vertPath,
                          const std::string& fragPath,
                          const std::string& geomPath) {
  auto base = exeDir();
  std::string vertSrc, fragSrc, geomSrc;
  if (!readFile(base / vertPath, vertSrc)) return 0;
  if (!readFile(base / fragPath, fragSrc)) return 0;
  if (!geomPath.empty() && !readFile(base / geomPath, geomSrc)) return 0;
  vertSrc = injectConstants(vertSrc);
  fragSrc = injectConstants(fragSrc);
  if (!geomSrc.empty()) geomSrc = injectConstants(geomSrc);

  GLuint vsId = glCreateShader(GL_VERTEX_SHADER);
  GLuint fsId = glCreateShader(GL_FRAGMENT_SHADER);
  GLuint gsId = geomPath.empty() ? 0 : glCreateShader(GL_GEOMETRY_SHADER);

  bool ok = compileStage(vsId, vertSrc.c_str(), vertPath.c_str()) &
            compileStage(fsId, fragSrc.c_str(), fragPath.c_str());
  if (gsId) ok = ok & compileStage(gsId, geomSrc.c_str(), geomPath.c_str());

  if (!ok) {
    glDeleteShader(vsId);
    glDeleteShader(fsId);
    if (gsId) glDeleteShader(gsId);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vsId);
  glAttachShader(program, fsId);
  if (gsId) glAttachShader(program, gsId);
  glLinkProgram(program);

  GLint linkStatus = GL_FALSE;
  GLint logLen = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
  if (logLen > 0) {
    std::vector<char> msg(logLen + 1);
    glGetProgramInfoLog(program, logLen, nullptr, msg.data());
    fprintf(stderr, "[link %s + %s%s%s] %s\n", vertPath.c_str(),
            fragPath.c_str(), geomPath.empty() ? "" : " + ", geomPath.c_str(),
            msg.data());
  }

  glDetachShader(program, vsId);
  glDetachShader(program, fsId);
  if (gsId) glDetachShader(program, gsId);
  glDeleteShader(vsId);
  glDeleteShader(fsId);
  if (gsId) glDeleteShader(gsId);

  if (linkStatus != GL_TRUE) {
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath) {
  m_id = linkProgram(vertexPath, fragmentPath, "");
}

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath,
               const std::string& geometryPath) {
  m_id = linkProgram(vertexPath, fragmentPath, geometryPath);
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
  // -1 (uniform stripped by the linker) is cached too — setting -1 is a
  // no-op, which keeps depth-only shaders from spamming stderr.
  GLint loc = glGetUniformLocation(m_id, name.c_str());
  m_locationCache[name] = loc;
  return loc;
}

void Shader::setInt(const std::string& name, int value) const {
  glUniform1i(getLocation(name), value);
}

void Shader::setFloat(const std::string& name, float value) const {
  glUniform1f(getLocation(name), value);
}

void Shader::setVec2(const std::string& name, float x, float y) const {
  glUniform2f(getLocation(name), x, y);
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

void Shader::setMat4Array(const std::string& name, int count,
                          const float* data) const {
  glUniformMatrix4fv(getLocation(name), count, GL_FALSE, data);
}

void Shader::setVec3Array(const std::string& name, int count,
                          const float* data) const {
  glUniform3fv(getLocation(name), count, data);
}
