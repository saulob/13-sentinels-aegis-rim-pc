// Stands in for <GLES3/gl3.h> in the one file that includes it: imgui's backend calls OpenGL ES by name, and these
// names go through pointers SDL resolves from ANGLE, so the executable links no OpenGL ES library.
#pragma once

#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_video.h>

// Desktop features the ES3 build must not see: the backend tests these macros, not its target.
#undef GL_CLIP_ORIGIN
#undef GL_POLYGON_MODE

#define TSR_IMGUI_GLES3_FUNCTIONS(X)                                                               \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                                       \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                           \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                                                         \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                 \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                                                     \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)                                     \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                                             \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                           \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                                                     \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                     \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                     \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                                       \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                                     \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                                     \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                                                   \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                                       \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                                           \
    X(PFNGLDETACHSHADERPROC, glDetachShader)                                                       \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)                               \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                 \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                           \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                                         \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                 \
    X(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation)                                             \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                             \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                       \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                               \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                         \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                                                           \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                           \
    X(PFNGLGETVERTEXATTRIBIVPROC, glGetVertexAttribiv)                                             \
    X(PFNGLGETVERTEXATTRIBPOINTERVPROC, glGetVertexAttribPointerv)                                 \
    X(PFNGLISPROGRAMPROC, glIsProgram)                                                             \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                         \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                                             \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                       \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                             \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                               \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                           \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

#define TSR_IMGUI_GLES3_CORE_FUNCTIONS(X)                                                          \
    X(glActiveTexture)                                                                             \
    X(glBindTexture)                                                                               \
    X(glDeleteTextures)                                                                            \
    X(glDisable)                                                                                   \
    X(glDrawElements)                                                                              \
    X(glEnable)                                                                                    \
    X(glGenTextures)                                                                               \
    X(glGetError)                                                                                  \
    X(glGetIntegerv)                                                                               \
    X(glGetString)                                                                                 \
    X(glIsEnabled)                                                                                 \
    X(glPixelStorei)                                                                               \
    X(glScissor)                                                                                   \
    X(glTexImage2D)                                                                                \
    X(glTexParameteri)                                                                             \
    X(glTexSubImage2D)                                                                             \
    X(glViewport)

#define TSR_DECLARE(type, name) static type tsr_##name;
TSR_IMGUI_GLES3_FUNCTIONS(TSR_DECLARE)
#undef TSR_DECLARE
#define TSR_DECLARE(name) static decltype(&::name) tsr_##name;
TSR_IMGUI_GLES3_CORE_FUNCTIONS(TSR_DECLARE)
#undef TSR_DECLARE

static bool LoadGles3ForImGui() {
#define TSR_RESOLVE(type, name)                                                                    \
    tsr_##name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));                             \
    if (!tsr_##name) {                                                                             \
        return false;                                                                              \
    }
#define TSR_RESOLVE_CORE(name)                                                                     \
    tsr_##name = reinterpret_cast<decltype(tsr_##name)>(SDL_GL_GetProcAddress(#name));             \
    if (!tsr_##name) {                                                                             \
        return false;                                                                              \
    }
    TSR_IMGUI_GLES3_FUNCTIONS(TSR_RESOLVE)
    TSR_IMGUI_GLES3_CORE_FUNCTIONS(TSR_RESOLVE_CORE)
#undef TSR_RESOLVE
#undef TSR_RESOLVE_CORE
    return true;
}

#define glActiveTexture tsr_glActiveTexture
#define glAttachShader tsr_glAttachShader
#define glBindBuffer tsr_glBindBuffer
#define glBindSampler tsr_glBindSampler
#define glBindTexture tsr_glBindTexture
#define glBindVertexArray tsr_glBindVertexArray
#define glBlendEquation tsr_glBlendEquation
#define glBlendEquationSeparate tsr_glBlendEquationSeparate
#define glBlendFuncSeparate tsr_glBlendFuncSeparate
#define glBufferData tsr_glBufferData
#define glBufferSubData tsr_glBufferSubData
#define glCompileShader tsr_glCompileShader
#define glCreateProgram tsr_glCreateProgram
#define glCreateShader tsr_glCreateShader
#define glDeleteBuffers tsr_glDeleteBuffers
#define glDeleteProgram tsr_glDeleteProgram
#define glDeleteSamplers tsr_glDeleteSamplers
#define glDeleteShader tsr_glDeleteShader
#define glDeleteTextures tsr_glDeleteTextures
#define glDeleteVertexArrays tsr_glDeleteVertexArrays
#define glDetachShader tsr_glDetachShader
#define glDisable tsr_glDisable
#define glDisableVertexAttribArray tsr_glDisableVertexAttribArray
#define glDrawElements tsr_glDrawElements
#define glEnable tsr_glEnable
#define glEnableVertexAttribArray tsr_glEnableVertexAttribArray
#define glGenBuffers tsr_glGenBuffers
#define glGenSamplers tsr_glGenSamplers
#define glGenTextures tsr_glGenTextures
#define glGenVertexArrays tsr_glGenVertexArrays
#define glGetAttribLocation tsr_glGetAttribLocation
#define glGetError tsr_glGetError
#define glGetIntegerv tsr_glGetIntegerv
#define glGetProgramInfoLog tsr_glGetProgramInfoLog
#define glGetProgramiv tsr_glGetProgramiv
#define glGetShaderInfoLog tsr_glGetShaderInfoLog
#define glGetShaderiv tsr_glGetShaderiv
#define glGetString tsr_glGetString
#define glGetStringi tsr_glGetStringi
#define glGetUniformLocation tsr_glGetUniformLocation
#define glGetVertexAttribPointerv tsr_glGetVertexAttribPointerv
#define glGetVertexAttribiv tsr_glGetVertexAttribiv
#define glIsEnabled tsr_glIsEnabled
#define glIsProgram tsr_glIsProgram
#define glLinkProgram tsr_glLinkProgram
#define glPixelStorei tsr_glPixelStorei
#define glSamplerParameteri tsr_glSamplerParameteri
#define glScissor tsr_glScissor
#define glShaderSource tsr_glShaderSource
#define glTexImage2D tsr_glTexImage2D
#define glTexParameteri tsr_glTexParameteri
#define glTexSubImage2D tsr_glTexSubImage2D
#define glUniform1i tsr_glUniform1i
#define glUniformMatrix4fv tsr_glUniformMatrix4fv
#define glUseProgram tsr_glUseProgram
#define glVertexAttribPointer tsr_glVertexAttribPointer
#define glViewport tsr_glViewport
