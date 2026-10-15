// libGLESv2.so / libGLESv3.so / libGLESv1_CM.so for AXRB Direct: there is
// never a current context, so queries return nothing (as GL does without one).
#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned int GLuint;

GLenum glGetError(void) { return 0; }
const GLubyte* glGetString(GLenum name) { return NULL; }
const GLubyte* glGetStringi(GLenum name, GLuint index) { return NULL; }
void glGetIntegerv(GLenum pname, GLint* data) {}
void glFlush(void) {}
void glFinish(void) {}

// Everything else is a no-op returning 0 (no object, no status): with no current
// context GL calls have no effect. AAPCS64 lets a stub ignore any arguments.
#define NOP(name) uint64_t name(void) { return 0; }
NOP(glActiveTexture) NOP(glAttachShader) NOP(glBindFramebuffer) NOP(glBindTexture) NOP(glBlitFramebuffer)
NOP(glCheckFramebufferStatus) NOP(glClear) NOP(glClearColor) NOP(glCompileShader) NOP(glCreateProgram)
NOP(glCreateShader) NOP(glDeleteFramebuffers) NOP(glDeleteProgram) NOP(glDeleteShader) NOP(glDeleteTextures)
NOP(glDrawArrays) NOP(glFramebufferTexture2D) NOP(glFramebufferTextureLayer) NOP(glGenFramebuffers)
NOP(glGenTextures) NOP(glGetProgramiv) NOP(glGetShaderInfoLog) NOP(glGetShaderiv) NOP(glGetUniformLocation)
NOP(glLinkProgram) NOP(glPixelStorei) NOP(glReadPixels) NOP(glShaderSource) NOP(glTexImage2D)
NOP(glTexParameteri) NOP(glTexStorage2D) NOP(glTexStorage3D) NOP(glUniform1i) NOP(glUniformMatrix4fv)
NOP(glUseProgram) NOP(glViewport) NOP(glEnable) NOP(glDisable) NOP(glBindBuffer) NOP(glGenBuffers)
NOP(glDeleteBuffers) NOP(glBufferData) NOP(glScissor) NOP(glDrawElements) NOP(glBindVertexArray)
NOP(glGenVertexArrays) NOP(glDeleteVertexArrays) NOP(glVertexAttribPointer) NOP(glEnableVertexAttribArray)
NOP(glDisableVertexAttribArray) NOP(glGetAttribLocation) NOP(glUniform1f) NOP(glUniform2f) NOP(glUniform4f)
NOP(glUniform4fv) NOP(glGetProgramInfoLog) NOP(glFenceSync) NOP(glClientWaitSync) NOP(glDeleteSync)
NOP(glReadBuffer) NOP(glDrawBuffers) NOP(glInvalidateFramebuffer) NOP(glColorMask) NOP(glDepthMask)

#include "gles_stubs.inc"
