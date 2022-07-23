#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_colour;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 frag_colour;

layout (binding = 0) uniform uniform_buffer_object {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform push_constant {
    mat4 transform;
} push;

void main() {
    gl_Position = push.transform * vec4(in_position, 1.0);
    frag_colour = in_colour;
}