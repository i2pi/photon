#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include "tracer.h"
#include "gl.h"

primitiveT	*CUBE;
primitiveT	*SPHERE;

float length_vector (vectorT *v) {
	// |v|
	float len;
	len = sqrt(v->x*v->x + v->y*v->y + v->z*v->z);
	return (len);	
}

void normalize_vector (vectorT *v) {
	// v / |v|
	float len = length_vector(v);
	v->x /= len;
	v->y /= len;
	v->z /= len;
}

void diff_vector (vectorT *a, vectorT *b, vectorT *v) {
	// v = a - b
	v->x = a->x - b->x;
	v->y = a->y - b->y;
	v->z = a->z - b->z;
}

float dist_vector (vectorT *a, vectorT *b) {
	vectorT d;
	diff_vector(a, b, &d);
	return (length_vector(&d));
}

float	dot_vector (vectorT *a, vectorT *b) {
	// a . b
	float d;
	d = a->x * b->x + 
		a->y * b->y +
		a->z * b->z;
	return (d);
}

void project_vector (vectorT *a, vectorT *b, vectorT *v) {
	// v = (a . b) / |a| * a;
	float 	d = dot_vector(a, b) / length_vector(a);
	*v = *a;
	v->x *= d;
	v->y *= d;
	v->z *= d;
}

primitiveT	*create_primitive(char *name, 
					void (*gl_draw)(float *p), 
					char (*ray_intersects)(float *p, rayT *r, vectorT *i), 
					int parameters, 
					...) {
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
	p->ray_intersects = ray_intersects;
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

objectT *create_object (int surfaces) {
	objectT *obj;

	obj = (objectT *) malloc (sizeof(objectT));
	
	obj->surfaces = surfaces;
	obj->surface = (surfaceT *) malloc (sizeof (surfaceT) * obj->surfaces);

	obj->surface->parameter = (float **) malloc (sizeof(float *) * obj->surfaces);

	return (obj);
}

objectT *create_cube_object (float x, float y, float z, float d) {
	objectT *obj;
	surfaceT *surf;	

	obj = create_object(1);

	surf = &obj->surface[0];	
	surf->primitive = CUBE;
	
	surf->parameter[0] = (float *) malloc (sizeof(float) * 4);
	surf->parameter[0][0] = x;
	surf->parameter[0][1] = y;
	surf->parameter[0][2] = z;
	surf->parameter[0][3] = d;

	return (obj);
}

objectT *create_sphere_object (float x, float y, float z, float r) {
	objectT *obj;
	surfaceT *surf;	

	obj = create_object(1);
	
	surf = &obj->surface[0];	
	surf->primitive = SPHERE;
	
	surf->parameter[0] = (float *) malloc (sizeof(float) * 4);
	surf->parameter[0][0] = x;
	surf->parameter[0][1] = y;
	surf->parameter[0][2] = z;
	surf->parameter[0][3] = r;

	return (obj);
}

char	null_ray_intersects (float *parameter, rayT *ray, vectorT *intersection) {
	return (0);
}

char	ray_sphere_intersection (float *parameter, rayT *ray, vectorT *intersection) {

	vectorT vpc;
	vectorT sphere_center;
	float	radius;
	float	vpc_len;

	sphere_center.x = parameter[0];
	sphere_center.y = parameter[1];
	sphere_center.z = parameter[2];
	radius = parameter[3];

	diff_vector(&ray->origin, &sphere_center, &vpc);
	vpc_len = length_vector(&vpc);
	
	if (dot_vector(&vpc, &ray->direction) < 0) {
		// The ray origin is inside the sphere
		if (vpc_len > radius) {
			// No intersection
			return (0);
		} else 
		if (vpc_len == radius) {
			// Glances surface
			*intersection = ray->origin;
		} else {
			// Pierces
			fprintf (stderr, "TODO\n");
			exit(-1);
		}
	} else {
		// Outside the sphere
		vectorT pc;

		//project_vector (&sphere_center, &ray->direction, &pc);
		project_vector (&ray->direction, &sphere_center, &pc);

		if (dist_vector(&sphere_center, &pc) > radius) {
			// No intersection
			return (0);
		} else {

			float dist = sqrt(
							powf(radius,2.0f) - 
							powf(dist_vector(&pc, &sphere_center), 2.0f)
						);
			float	d;

			if (vpc_len > radius) {
				d = dist_vector(&pc, &ray->origin) - dist;
			} else {
				d = dist_vector(&pc, &ray->origin) + dist;
			} 
			intersection->x = ray->origin.x + ray->direction.x * d;
			intersection->y = ray->origin.y + ray->direction.y * d;
			intersection->z = ray->origin.z + ray->direction.z * d;
		}
	}
	
	return (1);
}


void init_primitives (void) {
	CUBE = create_primitive("cube", cube_gl_draw, null_ray_intersects,
							4, "x", "y", "z", "d");
	SPHERE = create_primitive("sphere", sphere_gl_draw, ray_sphere_intersection,
							4, "x", "y", "z", "r");

	// todo: plane, triangle, lens
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

rayT	*cast_ray (rayT *ray, sceneT *scene, int depth) {

	// Find the nearest intersection

	// do this stupidly for now, just iterate through 
	// every primative and test for intersections
	// and then find the closest

	int	i, j;

	float	nearest_distance = 9e99;
	vectorT	nearest;
	char	found_hit = 0;

	surfaceT *surface;

	for (i=0; i<scene->objects; i++) {
		objectT *obj = scene->object[i];

		for (j=0; j<obj->surfaces; j++) {
			vectorT 	intersection;
			surfaceT 	*surf = &obj->surface[j];
			primitiveT *p = surf->primitive;

			if (p->ray_intersects(surf->parameter[j], ray, &intersection)) {
				// We have a hit!
				float	distance;

				found_hit = 1;

				distance = dist_vector(&intersection, &ray->origin);

				if (distance < nearest_distance)  {
					nearest_distance = distance;
					nearest = intersection;
					surface = surf;		
				}
			}
		}
	}

	if (!found_hit) return(NULL);

	// Send reflection ray
	// Send refraction rays

	// By Metropolis:
	// - Difussion 
	// - Scattering

	memcpy (ray->color, surface->color, sizeof(float) * 4);
	
	return (ray);
}
