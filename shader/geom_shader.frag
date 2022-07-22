#version 450

layout(location = 0) in vec3 frag_colour;

layout(location = 0) out vec4 out_colour;

layout(binding = 1) uniform sampler2D tex_sampler;

void main() {
    out_colour = texture(tex_sampler, frag_texcoord);
}