/*
 * SwitchGLES - Textured Fragment Shader (FFP)
 *
 * This shader samples from a 2D texture and modulates with vertex color.
 * Mimics GL_MODULATE texture environment mode.
 *
 * To compile with UAM:
 *   uam -s frag ffp_tex_fsh.glsl -o ffp_tex_fsh.dksh
 */

#version 460

/* Texture sampler - bound at binding point 0 */
layout(binding = 0) uniform sampler2D tex0;

/* Inputs from vertex shader */
layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in vec3 in_normal;

/* Output color */
layout(location = 0) out vec4 out_color;

void main()
{
    /* Sample texture */
    vec4 tex_color = texture(tex0, in_texcoord);

    /* Modulate texture with vertex color (GL_MODULATE mode) */
    out_color = tex_color * in_color;
}
