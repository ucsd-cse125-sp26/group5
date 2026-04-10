#include "shaders.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

static std::filesystem::path exeDir() {
  return std::filesystem::canonical("/proc/self/exe").parent_path();
}

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

GLuint loadShaders(const std::string& vertexPath,
                   const std::string& fragmentPath) {
  auto base = exeDir();
  std::string vertexShaderText = readFile(base / vertexPath);
  std::string fragmentShaderText = readFile(base / fragmentPath);

  GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
  GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);

  GLint Result = GL_FALSE;
  int InfoLogLength;

  // Compile Vertex Shader
  char const* VertexSourcePointer = vertexShaderText.c_str();
  glShaderSource(vertexShaderId, 1, &VertexSourcePointer, NULL);
  glCompileShader(vertexShaderId);

  glGetShaderiv(vertexShaderId, GL_COMPILE_STATUS, &Result);
  glGetShaderiv(vertexShaderId, GL_INFO_LOG_LENGTH, &InfoLogLength);
  if (InfoLogLength > 0) {
    std::vector<char> vertexErrorMessage(InfoLogLength + 1);
    glGetShaderInfoLog(vertexShaderId, InfoLogLength, NULL,
                       &vertexErrorMessage[0]);
    printf("%s\n", &vertexErrorMessage[0]);
  }

  // Compile Fragment Shader
  char const* FragmentSourcePointer = fragmentShaderText.c_str();
  glShaderSource(fragmentShaderId, 1, &FragmentSourcePointer, NULL);
  glCompileShader(fragmentShaderId);

  glGetShaderiv(fragmentShaderId, GL_COMPILE_STATUS, &Result);
  glGetShaderiv(fragmentShaderId, GL_INFO_LOG_LENGTH, &InfoLogLength);
  if (InfoLogLength > 0) {
    std::vector<char> FragmentShaderErrorMessage(InfoLogLength + 1);
    glGetShaderInfoLog(fragmentShaderId, InfoLogLength, NULL,
                       &FragmentShaderErrorMessage[0]);
    printf("%s\n", &FragmentShaderErrorMessage[0]);
  }

  // Link the program
  GLuint programID = glCreateProgram();
  glAttachShader(programID, vertexShaderId);
  glAttachShader(programID, fragmentShaderId);
  glLinkProgram(programID);

  glGetProgramiv(programID, GL_LINK_STATUS, &Result);
  glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &InfoLogLength);
  if (InfoLogLength > 0) {
    std::vector<char> ProgramErrorMessage(InfoLogLength + 1);
    glGetProgramInfoLog(programID, InfoLogLength, NULL,
                        &ProgramErrorMessage[0]);
    printf("%s\n", &ProgramErrorMessage[0]);
  }

  glDetachShader(programID, vertexShaderId);
  glDetachShader(programID, fragmentShaderId);

  glDeleteShader(vertexShaderId);
  glDeleteShader(fragmentShaderId);

  return programID;
}
