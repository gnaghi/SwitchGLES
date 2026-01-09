/*
 * SwitchGLES Extensions Header
 *
 * This header provides SwitchGLES-specific extensions that are not part
 * of the standard OpenGL ES 2.0 API.
 *
 * For deko3d, shaders are precompiled with explicit UBO bindings.
 * Use sglRegisterUniform() to map uniform names to their shader bindings
 * so that glGetUniformLocation() returns the correct location.
 */

#ifndef __gles2_gl2sgl_h_
#define __gles2_gl2sgl_h_ 1

#include <GLES2/gl2.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shader stage constants for sglRegisterUniform
 */
#define SGL_STAGE_VERTEX    0
#define SGL_STAGE_FRAGMENT  1

/*
 * sglRegisterUniform - Register a uniform name to a specific shader binding
 *
 * For deko3d precompiled shaders, you must register each uniform name
 * with its corresponding stage and binding number. This allows
 * glGetUniformLocation() to return the correct location.
 *
 * Parameters:
 *   name    - The uniform name as used in glGetUniformLocation()
 *   stage   - SGL_STAGE_VERTEX (0) or SGL_STAGE_FRAGMENT (1)
 *   binding - The binding number used in the shader (layout(binding = N))
 *
 * Returns:
 *   GL_TRUE on success, GL_FALSE on failure (e.g., table full)
 *
 * Example shader (lenny_vsh.glsl):
 *   layout(std140, binding = 0) uniform ModelView { mat4 mdlvMtx; };
 *   layout(std140, binding = 1) uniform Projection { mat4 projMtx; };
 *
 * Example registration:
 *   sglRegisterUniform("mdlvMtx", SGL_STAGE_VERTEX, 0);
 *   sglRegisterUniform("projMtx", SGL_STAGE_VERTEX, 1);
 *
 * After registration, glGetUniformLocation(program, "mdlvMtx") will
 * return the location encoding stage=0, binding=0.
 *
 * Note: Registrations are global (not per-program). Register once at startup.
 * The library has some common names pre-registered (u_mvp, u_color, etc).
 */
GL_APICALL GLboolean GL_APIENTRY sglRegisterUniform(const GLchar *name, GLint stage, GLint binding);

/*
 * sglClearUniformRegistry - Clear all registered uniform mappings
 *
 * Removes all user-registered uniform mappings. The built-in mappings
 * (u_mvp, u_color, etc.) are NOT cleared.
 */
GL_APICALL void GL_APIENTRY sglClearUniformRegistry(void);

/*
 * sgl_load_shader_from_file - Load a precompiled deko3d shader from file
 *
 * Parameters:
 *   shader - A shader object created with glCreateShader()
 *   path   - Path to the .dksh file (compiled with uam)
 *
 * Returns:
 *   true on success, false on failure
 *
 * Example:
 *   GLuint vs = glCreateShader(GL_VERTEX_SHADER);
 *   sgl_load_shader_from_file(vs, "romfs:/shaders/myshader_vsh.dksh");
 */
GL_APICALL GLboolean GL_APIENTRY sgl_load_shader_from_file(GLuint shader, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __gles2_gl2sgl_h_ */
