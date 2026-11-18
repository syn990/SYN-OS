/* ------------------------------------------------------------------------
 *   OpenGL 3.3 without a loader library: the 1.1 calls come from the
 *   system's library as they always have, and the newer ones this
 *   program uses are looked up once through SDL, the same on Linux and
 *   Windows.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_DIRSTAT_GL_H
#define SYN_DIRSTAT_GL_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <stdbool.h>

#define GL_NEWER(X) \
	X(GLuint, glCreateShader, (GLenum)) \
	X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
	X(void, glCompileShader, (GLuint)) \
	X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
	X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X(GLuint, glCreateProgram, (void)) \
	X(void, glAttachShader, (GLuint, GLuint)) \
	X(void, glLinkProgram, (GLuint)) \
	X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
	X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X(void, glUseProgram, (GLuint)) \
	X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
	X(void, glUniform1f, (GLint, GLfloat)) \
	X(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
	X(void, glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
	X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
	X(void, glGenVertexArrays, (GLsizei, GLuint *)) \
	X(void, glBindVertexArray, (GLuint)) \
	X(void, glGenBuffers, (GLsizei, GLuint *)) \
	X(void, glBindBuffer, (GLenum, GLuint)) \
	X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
	X(void, glEnableVertexAttribArray, (GLuint)) \
	X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
	X(void, glVertexAttribDivisor, (GLuint, GLuint)) \
	X(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))

#define X(ret, name, args) extern ret (APIENTRY *name) args;
GL_NEWER(X)
#undef X

/* Call with the context current. False, with the missing call named on
 * stderr, if the driver is not OpenGL 3.3. */
bool gl_load(void);

#endif
