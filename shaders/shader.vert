#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 fragUV;

layout(push_constant) uniform Camera {
    mat4 proj;
} cam;

out gl_PerVertex {
    vec4 gl_Position;
};

void main() {
    gl_Position = cam.proj * vec4(pos, 1.0);
    fragUV = uv;
}