#ifdef CPP_GLSL_INCLUDE
std::string fragment_shader_src = R"(
#endif

#version 440 core
in vec3 colorFromVert;

out vec4 FragColor;

void main() {
  FragColor = vec4(colorFromVert, 1.0);
}

#ifdef CPP_GLSL_INCLUDE
)";
#endif
