#ifdef CPP_GLSL_INCLUDE
std::string vertex_shader_src = R"(
#endif

#version 460 core
layout (location = 0) in vec3 position;
layout (location = 0) in vec3 color;

out vec3 colorFromVert;

void main() {
	gl_Position = vec4(position.x, position.y, position.z, 1.0);
	colorFromVert = color;
}

#ifdef CPP_GLSL_INCLUDE
)";
#endif
