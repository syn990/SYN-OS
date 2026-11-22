/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "gl.h"

#include <stdio.h>

#define X(ret, name, args) ret (APIENTRY *name) args;
GL_NEWER(X)
#undef X

void *(*gl_getproc)(const char *name);

bool gl_load(void) {
	void *(*get)(const char *) = gl_getproc ? gl_getproc : SDL_GL_GetProcAddress;
#define X(ret, name, args) \
	name = (ret (APIENTRY *) args)get(#name); \
	if (!name) { \
		fprintf(stderr, "syn-shell: the graphics driver has no %s (OpenGL 3.3 is needed)\n", #name); \
		return false; \
	}
	GL_NEWER(X)
#undef X
	return true;
}
