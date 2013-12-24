#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "tracer.h"
#include "gl.h"

primitiveT	*CUBE;
primitiveT	*SPHERE;

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
	}
	va_end(ap);

	return (p);
}

void cube_gl_draw(float *parameter) {	
	gl_cube(parameter[0], parameter[1], parameter[2], parameter[3]);
}

void sphere_gl_draw(float *parameter) {
	gl_sphere(parameter[0], parameter[1], parameter[2], parameter[3], 100);
}

objectT *create_object (int primitives) {
	objectT *obj;

	obj = (objectT *) malloc (sizeof(objectT));
	
	obj->primitives = primitives;
	obj->primitive = (primitiveT **) malloc (sizeof (primitiveT *) * obj->primitives);
	
	obj->parameter = (float **) malloc (sizeof(float *) * obj->primitives);

	return (obj);
}

objectT *create_cube_object (float x, float y, float z, float d) {
	objectT *obj;

	obj = create_object(1);
	
	obj->primitive[0] = CUBE;
	
	obj->parameter[0] = (float *) malloc (sizeof(float) * 4);
	obj->parameter[0][0] = x;
	obj->parameter[0][1] = y;
	obj->parameter[0][2] = z;
	obj->parameter[0][3] = d;

	return (obj);
}

objectT *create_sphere_object (float x, float y, float z, float r) {
	objectT *obj;

	obj = create_object(1);
	
	obj->primitive[0] = SPHERE;
	
	obj->parameter[0] = (float *) malloc (sizeof(float) * 4);
	obj->parameter[0][0] = x;
	obj->parameter[0][1] = y;
	obj->parameter[0][2] = z;
	obj->parameter[0][3] = r;

	return (obj);
}

void init_primitives (void) {
	CUBE = create_primitive("cube", cube_gl_draw, 4, "x", "y", "z", "d");
	SPHERE = create_primitive("sphere", sphere_gl_draw, 4, "x", "y", "z", "r");
}

sceneT *create_scene (void) {
	sceneT 	*s;

	s = (sceneT *) malloc (sizeof(sceneT));
	s->allocated = 1024;
	s->objects = 0;
	s->object = (objectT **) malloc (sizeof(objectT *) * s->allocated);

	return (s);
}

void	add_object_to_scene (sceneT *s, objectT *o) {
	if (s->objects >= s->allocated-1)  {
		fprintf (stderr, "TODO: grow scene\n");
		exit (-1);
	}

	s->object[s->objects++] = o;
}
