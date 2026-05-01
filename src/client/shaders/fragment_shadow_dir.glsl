#version 410 core
// Depth-only pass — fragment shader is empty so OpenGL's default depth write
// path runs. We keep the file (rather than a null frag) so the shader hot-
// reload pipeline has a uniform "vert + frag" contract.
void main() {}
