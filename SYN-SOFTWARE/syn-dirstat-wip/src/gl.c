/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "gl.h"

#include <stdio.h>

#define X(ret, name, args) ret (APIENTRY *name) args;
GL_NEWER(X)
#undef X

bool gl_load(void) {
#define X(ret, name, args) \
	name = (ret (APIENTRY *) args)SDL_GL_GetProcAddress(#name); \
	if (!name) { \
		fprintf(stderr, "syn-dirstat: the graphics driver has no %s (OpenGL 3.3 is needed)\n", #name); \
		return false; \
	}
	GL_NEWER(X)
#undef X
	return true;
}
