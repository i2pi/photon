#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "tracer.h"

extern primitiveT primitive[64];
extern int         primitives;


primitiveT	*create_primitive(char *name, int parameters, ...) {
	va_list	ap;
	int		i;
	
	primitiveT *p;

	p = (primitiveT *) malloc (sizeof(primitiveT));
	if (!p) {
		fprintf (stderr, "Failed to create new primitive '%s'\n", name);
		exit (-1);
	}

	p->name = strdup(name);
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

void init_primitives (void) {
	create_primitive("cube", 4, "x", "y", "z", "d");
	create_primitive("sphere", 4, "x", "y", "z", "r");
}
