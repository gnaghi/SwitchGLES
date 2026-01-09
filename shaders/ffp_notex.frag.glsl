/*
 * SwitchGLES - Fixed Function Pipeline Fragment Shader (No Texture)
 *
 * This shader handles fragments without texturing:
 * - Just vertex color interpolation
 *
 * To compile with UAM:
 *   uam -s frag ffp_notex.frag.glsl -o ffp_notex.frag.dksh
 */

#version 450

/* Inputs from vertex shader */
layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_texcoord;
layout(location = 2) in vec3 in_normal;

/* Output color */
layout(location = 0) out vec4 out_color;

void main()
{
    /* Output vertex color directly */
    out_color = in_color;
}
