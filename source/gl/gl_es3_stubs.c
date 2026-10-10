/*
 * SwitchGLES - OpenGL ES 2.0 / EGL implementation for Nintendo Switch
 * GL Layer - GLES 3.0 entry points not implemented yet
 *
 * Every GLES 3.0 function that SwitchGLES does not implement is defined here
 * so that code linked directly against GLES3/gl3.h links (dEQP-GLES3 built
 * with DEQP_GLES3_LIBRARIES, see docs/GLES3_PLAN.md). None of them does any
 * work: each one sets GL_INVALID_OPERATION, which is what Mesa's dispatch
 * reports for a function the context does not support, logs its first call
 * as a warning (the to-do list of an application run) and returns a neutral
 * value. They are not listed by eglGetProcAddress, so an engine probing for
 * an entry point still sees it as missing.
 *
 * A step of the plan that implements a function moves it out of this file.
 */

#include "gl_common.h"
#include <GLES3/gl3.h>
#include <stddef.h>

static void sgl_es3_unimplemented(const char *name, bool *logged) {
    if (!*logged) {
        *logged = true;
        SGL_WARN(SGL_LOG_CAT_CORE, "[CORE] %s: GLES 3.0 entry point not implemented", name);
    }
    sgl_context_t *ctx = sgl_get_current_context();
    if (ctx)
        sgl_set_error(ctx, GL_INVALID_OPERATION);
}

#define SGL_ES3_UNIMPLEMENTED()                                                                    \
    do {                                                                                           \
        static bool s_logged;                                                                      \
        sgl_es3_unimplemented(__func__, &s_logged);                                                \
    } while (0)

/* Framebuffers and render targets */

GL_APICALL void GL_APIENTRY glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) {
    (void)buffer;
    (void)drawbuffer;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) {
    (void)buffer;
    (void)drawbuffer;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) {
    (void)buffer;
    (void)drawbuffer;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth,
                                            GLint stencil) {
    (void)buffer;
    (void)drawbuffer;
    (void)depth;
    (void)stencil;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glFramebufferTextureLayer(GLenum target, GLenum attachment,
                                                      GLuint texture, GLint level, GLint layer) {
    (void)target;
    (void)attachment;
    (void)texture;
    (void)level;
    (void)layer;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glInvalidateFramebuffer(GLenum target, GLsizei numAttachments,
                                                    const GLenum *attachments) {
    (void)target;
    (void)numAttachments;
    (void)attachments;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments,
                                                       const GLenum *attachments, GLint x, GLint y,
                                                       GLsizei width, GLsizei height) {
    (void)target;
    (void)numAttachments;
    (void)attachments;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetInternalformativ(GLenum target, GLenum internalformat,
                                                  GLenum pname, GLsizei count, GLint *params) {
    (void)target;
    (void)internalformat;
    (void)pname;
    (void)count;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

/* Draws, instancing and vertex arrays */

GL_APICALL void GL_APIENTRY glDrawRangeElements(GLenum mode, GLuint start, GLuint end,
                                                GLsizei count, GLenum type, const void *indices) {
    (void)mode;
    (void)start;
    (void)end;
    (void)count;
    (void)type;
    (void)indices;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                                  GLsizei instancecount) {
    (void)mode;
    (void)first;
    (void)count;
    (void)instancecount;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                                                    const void *indices, GLsizei instancecount) {
    (void)mode;
    (void)count;
    (void)type;
    (void)indices;
    (void)instancecount;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    (void)n;
    (void)arrays;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGenVertexArrays(GLsizei n, GLuint *arrays) {
    (void)n;
    (void)arrays;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                                                   GLsizei stride, const void *pointer) {
    (void)index;
    (void)size;
    (void)type;
    (void)stride;
    (void)pointer;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetVertexAttribIiv(GLuint index, GLenum pname, GLint *params) {
    (void)index;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params) {
    (void)index;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
    (void)index;
    (void)x;
    (void)y;
    (void)z;
    (void)w;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z,
                                               GLuint w) {
    (void)index;
    (void)x;
    (void)y;
    (void)z;
    (void)w;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glVertexAttribI4iv(GLuint index, const GLint *v) {
    (void)index;
    (void)v;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glVertexAttribI4uiv(GLuint index, const GLuint *v) {
    (void)index;
    (void)v;
    SGL_ES3_UNIMPLEMENTED();
}

/* 3D / array textures and immutable storage */

GL_APICALL void GL_APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                            GLint yoffset, GLint zoffset, GLsizei width,
                                            GLsizei height, GLsizei depth, GLenum format,
                                            GLenum type, const void *pixels) {
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)zoffset;
    (void)width;
    (void)height;
    (void)depth;
    (void)format;
    (void)type;
    (void)pixels;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                                GLint yoffset, GLint zoffset, GLint x, GLint y,
                                                GLsizei width, GLsizei height) {
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)zoffset;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glCompressedTexImage3D(GLenum target, GLint level,
                                                   GLenum internalformat, GLsizei width,
                                                   GLsizei height, GLsizei depth, GLint border,
                                                   GLsizei imageSize, const void *data) {
    (void)target;
    (void)level;
    (void)internalformat;
    (void)width;
    (void)height;
    (void)depth;
    (void)border;
    (void)imageSize;
    (void)data;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                                      GLint yoffset, GLint zoffset, GLsizei width,
                                                      GLsizei height, GLsizei depth, GLenum format,
                                                      GLsizei imageSize, const void *data) {
    (void)target;
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)zoffset;
    (void)width;
    (void)height;
    (void)depth;
    (void)format;
    (void)imageSize;
    (void)data;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                                           GLsizei width, GLsizei height) {
    (void)target;
    (void)levels;
    (void)internalformat;
    (void)width;
    (void)height;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                                           GLsizei width, GLsizei height, GLsizei depth) {
    (void)target;
    (void)levels;
    (void)internalformat;
    (void)width;
    (void)height;
    (void)depth;
    SGL_ES3_UNIMPLEMENTED();
}

/* Sampler objects */

GL_APICALL void GL_APIENTRY glGenSamplers(GLsizei count, GLuint *samplers) {
    (void)count;
    (void)samplers;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glDeleteSamplers(GLsizei count, const GLuint *samplers) {
    (void)count;
    (void)samplers;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
    (void)sampler;
    (void)pname;
    (void)param;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glSamplerParameteriv(GLuint sampler, GLenum pname, const GLint *param) {
    (void)sampler;
    (void)pname;
    (void)param;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
    (void)sampler;
    (void)pname;
    (void)param;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glSamplerParameterfv(GLuint sampler, GLenum pname,
                                                 const GLfloat *param) {
    (void)sampler;
    (void)pname;
    (void)param;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint *params) {
    (void)sampler;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat *params) {
    (void)sampler;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

/* Buffer objects: mapping, copies, indexed bindings */

GL_APICALL void *GL_APIENTRY glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                                              GLbitfield access) {
    (void)target;
    (void)offset;
    (void)length;
    (void)access;
    SGL_ES3_UNIMPLEMENTED();
    return NULL;
}

GL_APICALL void GL_APIENTRY glFlushMappedBufferRange(GLenum target, GLintptr offset,
                                                     GLsizeiptr length) {
    (void)target;
    (void)offset;
    (void)length;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL GLboolean GL_APIENTRY glUnmapBuffer(GLenum target) {
    (void)target;
    SGL_ES3_UNIMPLEMENTED();
    return GL_FALSE;
}

GL_APICALL void GL_APIENTRY glGetBufferPointerv(GLenum target, GLenum pname, void * *params) {
    (void)target;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glCopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                                                GLintptr readOffset, GLintptr writeOffset,
                                                GLsizeiptr size) {
    (void)readTarget;
    (void)writeTarget;
    (void)readOffset;
    (void)writeOffset;
    (void)size;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) {
    (void)target;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

/* Uniforms: unsigned, non-square matrices, uniform blocks */

GL_APICALL void GL_APIENTRY glUniform1ui(GLint location, GLuint v0) {
    (void)location;
    (void)v0;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniform2ui(GLint location, GLuint v0, GLuint v1) {
    (void)location;
    (void)v0;
    (void)v1;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
    (void)location;
    (void)v0;
    (void)v1;
    (void)v2;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2,
                                         GLuint v3) {
    (void)location;
    (void)v0;
    (void)v1;
    (void)v2;
    (void)v3;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniform1uiv(GLint location, GLsizei count, const GLuint *value) {
    (void)location;
    (void)count;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniform2uiv(GLint location, GLsizei count, const GLuint *value) {
    (void)location;
    (void)count;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniform3uiv(GLint location, GLsizei count, const GLuint *value) {
    (void)location;
    (void)count;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniform4uiv(GLint location, GLsizei count, const GLuint *value) {
    (void)location;
    (void)count;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetUniformuiv(GLuint program, GLint location, GLuint *params) {
    (void)program;
    (void)location;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose,
                                                 const GLfloat *value) {
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose,
                                                 const GLfloat *value) {
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose,
                                                 const GLfloat *value) {
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose,
                                                 const GLfloat *value) {
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose,
                                                 const GLfloat *value) {
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose,
                                                 const GLfloat *value) {
    (void)location;
    (void)count;
    (void)transpose;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetUniformIndices(GLuint program, GLsizei uniformCount,
                                                const GLchar *const *uniformNames,
                                                GLuint *uniformIndices) {
    (void)program;
    (void)uniformCount;
    (void)uniformNames;
    (void)uniformIndices;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetActiveUniformsiv(GLuint program, GLsizei uniformCount,
                                                  const GLuint *uniformIndices, GLenum pname,
                                                  GLint *params) {
    (void)program;
    (void)uniformCount;
    (void)uniformIndices;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL GLuint GL_APIENTRY glGetUniformBlockIndex(GLuint program,
                                                     const GLchar *uniformBlockName) {
    (void)program;
    (void)uniformBlockName;
    SGL_ES3_UNIMPLEMENTED();
    return GL_INVALID_INDEX;
}

GL_APICALL void GL_APIENTRY glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex,
                                                      GLenum pname, GLint *params) {
    (void)program;
    (void)uniformBlockIndex;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex,
                                                        GLsizei bufSize, GLsizei *length,
                                                        GLchar *uniformBlockName) {
    (void)program;
    (void)uniformBlockIndex;
    (void)bufSize;
    (void)length;
    (void)uniformBlockName;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                                                  GLuint uniformBlockBinding) {
    (void)program;
    (void)uniformBlockIndex;
    (void)uniformBlockBinding;
    SGL_ES3_UNIMPLEMENTED();
}

/* Programs: fragment outputs, binaries */

GL_APICALL GLint GL_APIENTRY glGetFragDataLocation(GLuint program, const GLchar *name) {
    (void)program;
    (void)name;
    SGL_ES3_UNIMPLEMENTED();
    return -1;
}

GL_APICALL void GL_APIENTRY glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length,
                                               GLenum *binaryFormat, void *binary) {
    (void)program;
    (void)bufSize;
    (void)length;
    (void)binaryFormat;
    (void)binary;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glProgramBinary(GLuint program, GLenum binaryFormat, const void *binary,
                                            GLsizei length) {
    (void)program;
    (void)binaryFormat;
    (void)binary;
    (void)length;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glProgramParameteri(GLuint program, GLenum pname, GLint value) {
    (void)program;
    (void)pname;
    (void)value;
    SGL_ES3_UNIMPLEMENTED();
}

/* Query objects */

GL_APICALL void GL_APIENTRY glGenQueries(GLsizei n, GLuint *ids) {
    (void)n;
    (void)ids;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glDeleteQueries(GLsizei n, const GLuint *ids) {
    (void)n;
    (void)ids;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL GLboolean GL_APIENTRY glIsQuery(GLuint id) {
    (void)id;
    SGL_ES3_UNIMPLEMENTED();
    return GL_FALSE;
}

GL_APICALL void GL_APIENTRY glBeginQuery(GLenum target, GLuint id) {
    (void)target;
    (void)id;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glEndQuery(GLenum target) {
    (void)target;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params) {
    (void)id;
    (void)pname;
    (void)params;
    SGL_ES3_UNIMPLEMENTED();
}

/* Transform feedback (needs deko3d support, see the plan) */

GL_APICALL void GL_APIENTRY glBeginTransformFeedback(GLenum primitiveMode) {
    (void)primitiveMode;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glEndTransformFeedback(void) {
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glTransformFeedbackVaryings(GLuint program, GLsizei count,
                                                        const GLchar *const *varyings,
                                                        GLenum bufferMode) {
    (void)program;
    (void)count;
    (void)varyings;
    (void)bufferMode;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGetTransformFeedbackVarying(GLuint program, GLuint index,
                                                          GLsizei bufSize, GLsizei *length,
                                                          GLsizei *size, GLenum *type,
                                                          GLchar *name) {
    (void)program;
    (void)index;
    (void)bufSize;
    (void)length;
    (void)size;
    (void)type;
    (void)name;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids) {
    (void)n;
    (void)ids;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glGenTransformFeedbacks(GLsizei n, GLuint *ids) {
    (void)n;
    (void)ids;
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glPauseTransformFeedback(void) {
    SGL_ES3_UNIMPLEMENTED();
}

GL_APICALL void GL_APIENTRY glResumeTransformFeedback(void) {
    SGL_ES3_UNIMPLEMENTED();
}


