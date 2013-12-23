#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "tracer.h"

#include "gl.h"

extern primitiveT primitive[64];
extern int         primitives;


primitiveT	*create_primitive(char *name, void (*gl_draw)(float *p), int parameters, ...) {
	va_list	ap;
	int		i;
	
	primitiveT *p;

	p = (primitiveT *) malloc (sizeof(primitiveT));
	if (!p) {
		fprintf (stderr, "Failed to create new primitive '%s'\n", name);
		exit (-1);
	}

	p->name = strdup(name);
	p->gl_draw = gl_draw;
	p->parameters = parameters;

	p->parameter_name = (char **) malloc (sizeof(char *) * parameters);

	va_start (ap, parameters);
	for (i=0; i<parameters; i++) {
		p->parameter_name[i] = strdup(va_arg(ap, char *));
		printf ("%d: '%s'\n", i, p->parameter_name[i]);
	}
	va_end(ap);

	return (p);
}

void cube_gl_draw(float *parameter) {	
	gl_cube(parameter[0], parameter[1], parameter[3], parameter[4]);
}

void sphere_gl_draw(float *parameter) {
	gl_sphere(parameter[0], parameter[1], parameter[3], parameter[4], 100);
}

void init_primitives (void) {
	create_primitive("cube", cube_gl_draw, 4, "x", "y", "z", "d");
	create_primitive("sphere", sphere_gl_draw, 4, "x", "y", "z", "r");
}
