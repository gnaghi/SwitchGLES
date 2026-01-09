/*
 * SwitchGLES - Fixed Function Pipeline Basic Fragment Shader
 *
 * This shader handles basic fragment operations:
 * - Vertex color interpolation
 * - Optional texture sampling (when bound)
 *
 * To compile with UAM:
 *   uam -s frag ffp_basic.frag.glsl -o ffp_basic.frag.dksh
 */

#version 450

/* Texture sampler - binding 0 */
layout(binding = 0) uniform sampler2D tex0;

/* Inputs from vertex shader */
layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in vec3 in_normal;

/* Output color */
layout(location = 0) out vec4 out_color;

void main()
{
    /* Start with vertex color */
    vec4 color = in_color;

    /* Sample texture and modulate
     * Note: In a real implementation, we'd have a uniform flag
     * to indicate whether texturing is enabled. For now, we'll
     * check if texture coordinates are being used.
     */
    vec4 tex_color = texture(tex0, in_texcoord);

    /* Simple modulate: vertex_color * texture_color
     * When no texture is bound, we'd need a 1x1 white texture
     * as a fallback (common technique)
     */
    color = color * tex_color;

    out_color = color;
}
