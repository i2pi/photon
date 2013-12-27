#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include "tracer.h"
#include "gl.h"

primitiveT	*SPHERE;
primitiveT	*TRIANGLE;

void array_to_vector(float *arr, vectorT *v) {
	v->x = arr[0];
	v->y = arr[1];
	v->z = arr[2];
}

void vector_to_array(vectorT *v, float *arr) {
	arr[0] = v->x;
	arr[1] = v->y;
	arr[2] = v->z;
}

void params_to_array(float x, float y, float z, float *arr) {
	arr[0] = x;
	arr[1] = y;
	arr[2] = z;
}

void params_to_vector(float x, float y, float z, vectorT *v) {
	v->x = x;
	v->y = y;
	v->z = z;
}

void flat_triangle_to_array (vectorT v1, vectorT v2, vectorT v3, vectorT norm, float *triangle) {
	vector_to_array(&v1, &triangle[0]);
	vector_to_array(&v2, &triangle[3]);
	vector_to_array(&v3, &triangle[6]);
	vector_to_array(&norm, &triangle[9]);
}

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

float	cosine_vector (vectorT *a, vectorT *b) {
	// (a . b) / (|a| |b|)
	return (dot_vector(a,b) / (length_vector(a) * length_vector(b)));
}

void cross_vector(vectorT *a, vectorT *b, vectorT *v) {
	// v = a x b
	v->x = a->y * b->z - b->y * a->z; 
	v->y = a->z * b->x - b->z * a->x; 
	v->z = a->x * b->y - b->x * a->y;
}

void project_vector (vectorT *a, vectorT *b, vectorT *v) {
	// v = (a . b) / |a| * a;
	float 	d = dot_vector(a, b) / length_vector(a);
	*v = *a;
	v->x *= d;
	v->y *= d;
	v->z *= d;
}

void project_point_on_ray (rayT *ray, vectorT *point, vectorT *projection) {
	vectorT pr;
	float   d;

	diff_vector (point, &ray->origin, &pr);
	d = dot_vector(&pr, &ray->direction);
    
	scale_offset_vector(&ray->origin, &ray->direction, d, projection);
}

void scale_vector (vectorT *v, float s) {
	// v = s * v
	v->x *= s;
	v->y *= s;
	v->z *= s;
}

void scale_offset_vector (vectorT *a, vectorT *d, float s, vectorT *v) {
	// v = a + (s * d)

	v->x = a->x + s * d->x;
	v->y = a->y + s * d->y;
	v->z = a->z + s * d->z;
}

void reflect_vector (vectorT *v, vectorT *n, vectorT *r) {
	// r = v - 2*n*(v . n)
	float	dot;
	vectorT a;

	a = *n;

	dot = dot_vector(v, n);

	scale_vector(&a, 2.0 * dot);
	diff_vector(v, &a, r);
	normalize_vector(r);
}

void triangle_normal_vector (vectorT *a, vectorT *b, vectorT *c, vectorT *n) {
	vectorT v1, v2;

	diff_vector(a, b, &v1);
	diff_vector(a, c, &v2);
	cross_vector(&v1, &v2, n);
	normalize_vector(n);
}

primitiveT	*create_primitive(char *name, 
					void (*gl_draw)(float *p), 
					char (*ray_intersects)(float *p, rayT *r, vectorT *i), 
					void (*normal)(float *p, vectorT *pt, vectorT *n),
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
	p->normal = normal;
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
	gl_sphere(parameter[0], parameter[1], parameter[2], parameter[3], 20);
}

void triangle_gl_draw(float *parameter) {
	float normal[9];
	int	i;
	for (i=0; i<3; i++) {
		normal[3*i + 0] = parameter[9];
		normal[3*i + 1] = parameter[10];
		normal[3*i + 2] = parameter[11];
	}
	gl_triangle(parameter, normal);
}

objectT *create_object (int surfaces) {
	objectT *obj;

	obj = (objectT *) malloc (sizeof(objectT));
	
	obj->surfaces = surfaces;
	obj->surface = (surfaceT *) malloc (sizeof (surfaceT) * obj->surfaces);

	return (obj);
}

void	init_surface (primitiveT *p, surfaceT *surf) {
	surf->primitive = p;	
	surf->parameter = (float *) malloc (sizeof(float) * p->parameters);
	surf->properties.reflectance = 0.0f;
	surf->properties.roughness = 0.0f;
}

objectT *create_cube_object (float x, float y, float z, float d) {
	objectT *obj;
	int	i;
	vectorT cube[8];
	vectorT	norm;

	obj = create_object(12);

	for (i=0; i<obj->surfaces; i++) {
		init_surface(TRIANGLE, &obj->surface[i]);
	}

	params_to_vector (x-d, y+d, z-d, &cube[0]);  // Front, top, left
	params_to_vector (x+d, y+d, z-d, &cube[1]);  // Front, top, right 
	params_to_vector (x+d, y-d, z-d, &cube[2]);  // Front, bottom, right 
	params_to_vector (x-d, y-d, z-d, &cube[3]);  // Front, bottom, left

	params_to_vector (x-d, y+d, z+d, &cube[4]);  // Back, top, left
	params_to_vector (x+d, y+d, z+d, &cube[5]);  // Back, top, right 
	params_to_vector (x+d, y-d, z+d, &cube[6]);  // Back, bottom, right 
	params_to_vector (x-d, y-d, z+d, &cube[7]);  // Back, bottom, left

	// Back (0, 1, 2, 3)
	norm.x = 0; norm.y = 0; norm.z = -1;
	flat_triangle_to_array(cube[0], cube[1], cube[2], norm, obj->surface[0].parameter);
	flat_triangle_to_array(cube[0], cube[3], cube[2], norm, obj->surface[1].parameter);

	// Front (4, 5, 6, 7)
	norm.x = 0; norm.y = 0; norm.z = 1;
	flat_triangle_to_array(cube[4], cube[5], cube[6], norm, obj->surface[2].parameter);
	flat_triangle_to_array(cube[4], cube[7], cube[6], norm, obj->surface[3].parameter);

	// Top (0, 1, 4, 5)
	norm.x = 0; norm.y = 1; norm.z = 0;
	flat_triangle_to_array(cube[0], cube[1], cube[4], norm, obj->surface[4].parameter);
	flat_triangle_to_array(cube[4], cube[5], cube[1], norm, obj->surface[5].parameter);


	// Bottom (2, 3, 6, 7)
	norm.x = 0; norm.y = -1; norm.z = 0;
	flat_triangle_to_array(cube[2], cube[3], cube[6], norm, obj->surface[6].parameter);
	flat_triangle_to_array(cube[6], cube[7], cube[3], norm, obj->surface[7].parameter);

	// Left (0, 3, 4, 7)
	norm.x = -1; norm.y = 0; norm.z = 0;
	flat_triangle_to_array(cube[0], cube[3], cube[4], norm, obj->surface[8].parameter);
	flat_triangle_to_array(cube[4], cube[7], cube[3], norm, obj->surface[9].parameter);

	// Right (1, 2, 5, 6)
	norm.x = 1; norm.y = 0; norm.z = 0;
	flat_triangle_to_array(cube[1], cube[2], cube[5], norm, obj->surface[10].parameter);
	flat_triangle_to_array(cube[5], cube[6], cube[2], norm, obj->surface[11].parameter);

	return (obj);
}

objectT *create_sphere_object (float x, float y, float z, float r) {
	objectT *obj;
	surfaceT *surf;	

	obj = create_object(1);
	
	surf = &obj->surface[0];	
	init_surface (SPHERE, surf);
	surf->parameter[0] = x;
	surf->parameter[1] = y;
	surf->parameter[2] = z;
	surf->parameter[3] = r;

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
	vectorT	pc;

	sphere_center.x = parameter[0];
	sphere_center.y = parameter[1];
	sphere_center.z = parameter[2];
	radius = parameter[3];

	diff_vector(&sphere_center, &ray->origin, &vpc);
	vpc_len = length_vector(&vpc);

	project_point_on_ray (ray, &sphere_center, &pc);
	
	if (dot_vector(&vpc, &ray->direction) < 0) {
		if (vpc_len > radius) {
			return (0);
		} else 
		if (vpc_len == radius) {
			// Ray starts at surface
			*intersection = ray->origin;
			return (1);
		} else {
			float	dist, d;

			dist = sqrt(
							powf(radius,2.0f) - 
							powf(dist_vector(&pc, &sphere_center), 2.0f)
						);
			d = dist - dist_vector(&pc, &ray->origin);
	
			scale_offset_vector (&ray->origin, &ray->direction, d, intersection);
			return (1);
		}
	} else {
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
			scale_offset_vector (&ray->origin, &ray->direction, d, intersection);
			return (1);
		}
	}

	fprintf (stderr, "shouldn't be here\n");
	exit (-1);	
	return (0);
}

char	ray_triangle_intersection (float *parameter, rayT *ray, vectorT *intersection) {
	vectorT	p, d, v0, v1, v2;
	vectorT	e1, e2, h, s, q;
	float 	a, f, u, v, t;

	p = ray->origin;
	d = ray->direction;

	array_to_vector(&parameter[0], &v0);
	array_to_vector(&parameter[3], &v1);
	array_to_vector(&parameter[6], &v2);

	diff_vector(&v1, &v0, &e1);
	diff_vector(&v2, &v0, &e2);

	cross_vector(&d, &e2, &h);
	a = dot_vector(&e1, &h);

	if (a > -0.00001 && a < 0.00001) {
		return (0);
	}

	f = 1.0 / a;
	diff_vector (&p, &v0, &s);
	u = f * dot_vector(&s, &h);

	if (u < 0.0 || u > 1.0) {
		return (0);
	}

	cross_vector(&s, &e1, &q);
	v = f * dot_vector(&d, &q);

	if (v < 0.0 || u + v > 1.0) {
		return (0);
	}

	t = f * dot_vector(&e2, &q);

	if (t > 0.00001) {
		scale_offset_vector (&ray->origin, &ray->direction, t, intersection);
		return (1);
	} 

	return (0);
}

void color_object (objectT *obj, float *color, float reflectance, float roughness) {
	int	i;

	for (i=0; i<obj->surfaces; i++) {
		memcpy(obj->surface[i].color, color, sizeof(float) * 4);
		obj->surface[i].properties.reflectance = reflectance;
		obj->surface[i].properties.roughness = roughness;
	}
}

void	sphere_normal(float *parameter, vectorT *point, vectorT *normal) {
	vectorT center;
	array_to_vector(parameter, &center);
	diff_vector(point, &center, normal);
	normalize_vector(normal);
}

void	triangle_normal(float *parameter, vectorT *point, vectorT *normal) {
	array_to_vector(&parameter[9], normal);
}

void init_primitives (void) {
	//CUBE = create_primitive("cube", cube_gl_draw, null_ray_intersects,
	//						4, "x", "y", "z", "d");
	SPHERE = create_primitive("sphere", sphere_gl_draw, ray_sphere_intersection, sphere_normal,
							4, "x", "y", "z", "r");
	TRIANGLE = create_primitive("triangle", triangle_gl_draw, ray_triangle_intersection, triangle_normal,
							12, "x1", "y1", "z1", 
							   "x2", "y2", "z2", 
							   "x3", "y3", "z3",
							   "nx", "ny", "nz");

	// todo: plane, lens, aperture
}

sceneT *create_scene (void) {
	sceneT 	*s;

	s = (sceneT *) malloc (sizeof(sceneT));
	s->object_array_size = 1024;
	s->objects = 0;
	s->object = (objectT **) malloc (sizeof(objectT *) * s->object_array_size);

	s->light_array_size = 8;
	s->lights = 0;
	s->light = (lightT **) malloc (sizeof(lightT *) * s->light_array_size);

	params_to_vector(0,0,1, &s->camera);

	return (s);
}

void	add_object_to_scene (sceneT *s, objectT *o) {
	if (s->objects >= s->object_array_size-1)  {
		fprintf (stderr, "TODO: grow scene objects\n");
		exit (-1);
	}

	s->object[s->objects++] = o;
}

void	add_light_to_scene (sceneT *s, lightT *l) {
	GLenum gl_lights[] = {GL_LIGHT0, GL_LIGHT1, GL_LIGHT2, GL_LIGHT3, GL_LIGHT4};

	if (s->lights >= s->light_array_size-1)  {
		fprintf (stderr, "TODO: grow scene lights\n");
		exit (-1);
	}

	l->GL_LIGHT = gl_lights[s->lights];

	s->light[s->lights++] = l;
}

void positional_light_gl_draw (lightT *self) {
	gl_positional_light(self->GL_LIGHT, 
		self->position.x, self->position.y, self->position.z, self->color);
}

lightT	*create_positional_light (float x, float y, float z, float color[4]) {
	lightT *l;
	

	l = (lightT *) malloc (sizeof(lightT));

	params_to_vector (x,y,z, &l->position);
	
	memcpy (l->color, color, sizeof(float)*4);

	l->gl_draw = positional_light_gl_draw;

	return (l);
}

objectT *create_checkerboard_object (float y, float width, int n) {
    int i, j;
    objectT *obj;
    float w = 2.0 * width / (float) (n-1);
	vectorT norm;

	params_to_vector (0, 1, 0, &norm);

    obj = create_object (n * n * 2);    

	for (i=0; i<obj->surfaces; i++) {
		init_surface(TRIANGLE, &obj->surface[i]);
	}

    for (i=0; i<n; i++)
    for (j=0; j<n; j++) {
		float x,z;
		float color[4];
		float ref;
		surfaceT *s;

		if ((i+j) % 2) {
			color[0] = 0.8;
			color[1] = 0.8;
			color[2] = 0.8;
			color[3] = 1.0;	
			ref = 0.2;
		} else {
			color[0] = 0.0;	
			color[1] = 0.0;	
			color[2] = 0.0;
			color[3] = 1.0;	
			ref = 1.0;
		}

		x = 2.0*width*((i / (float)(n-1)) - 0.5);
		z = 2.0*width*((j / (float)(n-1)) - 0.5);
		
        s = &obj->surface[(i*n + j)*2 + 0];
		params_to_array (x,y,z, &s->parameter[0]);
		params_to_array (x+w,y,z, &s->parameter[3]);
		params_to_array (x+w,y,z+w, &s->parameter[6]);
		vector_to_array (&norm, &s->parameter[9]);
		memcpy (s->color, color, sizeof(float) * 4);
		s->properties.reflectance = ref;

        s = &obj->surface[(i*n + j)*2 + 1];
		params_to_array (x,y,z, &s->parameter[0]);
		params_to_array (x,y,z+w, &s->parameter[3]);
		params_to_array (x+w,y,z+w, &s->parameter[6]);
		vector_to_array (&norm, &s->parameter[9]);
		memcpy (s->color, color, sizeof(float) * 4);
		s->properties.reflectance = ref;
    }

    return (obj);
}

char	line_of_sight(sceneT *scene, vectorT *a, vectorT *b) {
	// Can a see b in this scene?

	rayT	ray;
	int	i, j;

	ray.origin = *a;
	diff_vector(b, a, &ray.direction);
	normalize_vector(&ray.direction);

	// again, done stupidly

	for (i=0; i<scene->objects; i++) {
		objectT *obj = scene->object[i];

		for (j=0; j<obj->surfaces; j++) {
			vectorT 	intersection;
			surfaceT 	*surf = &obj->surface[j];
			primitiveT *p = surf->primitive;

			if (p->ray_intersects(surf->parameter, &ray, &intersection)) {
				// Make sure we're not self-intersecting...
				if ((dist_vector(a, &intersection) > 0.001) &&
				    (dist_vector(b, &intersection) > 0.001)) {
					return (0);
				}
			}
		}
	}

	return (1);
}

void perturb_vector (vectorT *a, float s, vectorT *p) {
	// todo: uniform sampling in a cone
	p->x = a->x + s*(1.0 - 0.5*random() / (float) RAND_MAX);	
	p->y = a->y + s*(1.0 - 0.5*random() / (float) RAND_MAX);	
	p->z = a->z + s*(1.0 - 0.5*random() / (float) RAND_MAX);	
	normalize_vector(p);
}


rayT	*cast_ray (rayT *ray, sceneT *scene, int depth) {

	// Find the nearest intersection

	// do this stupidly for now, just iterate through 
	// every primative and test for intersections
	// and then find the closest

	int	i, j;

	float	nearest_distance = 9e99;
	vectorT	nearest_intersection;
	char	found_hit = 0;

	surfaceT *surface;
	vectorT	 normal;

	if (depth < 0) return (ray); 

	for (i=0; i<scene->objects; i++) {
		objectT *obj = scene->object[i];

		for (j=0; j<obj->surfaces; j++) {
			vectorT 	intersection;
			surfaceT 	*surf = &obj->surface[j];
			primitiveT *p = surf->primitive;

			if (p->ray_intersects(surf->parameter, ray, &intersection)) {
				// We have a hit!
				float	distance;

				distance = dist_vector(&intersection, &ray->origin);

				if ((distance < nearest_distance) && (distance > 0.001))  {
					found_hit = 1;
					nearest_distance = distance;
					nearest_intersection = intersection;
					surface = surf;		
				}
			}
		}
	}

	if (!found_hit) return(NULL);

	// Get incident light at the point of intersection on the surface

	for (i=0; i<4; i++) ray->color[i] = 0;

	surface->primitive->normal(surface->parameter, &nearest_intersection, &normal);

	for (i=0; i<scene->lights; i++) {
		lightT	*light;
		vectorT	incidence;
		float	distance;
		float	cosine;

		light = scene->light[i];

		if (!line_of_sight(scene, &nearest_intersection, &light->position)) continue;

		diff_vector(&light->position, &nearest_intersection, &incidence);

		distance = length_vector(&incidence);
		normalize_vector(&incidence);

		cosine = cosine_vector(&normal, &incidence);

		if (cosine <= 0) continue;

		for (j=0; j<4; j++) {
			ray->color[j] += surface->color[j] * light->color[j] * cosine / 
				(1 + distance * 0.01);
		}
	}

	// Send reflection ray

	if (surface->properties.reflectance > 0) {
		rayT	reflection_ray, *ret;
		vectorT	rough_normal;
		int	j, N = 1;

		for (i=0; i<4; i++) reflection_ray.color[i] = 0;
	
		// todo: better mechanism for sampling rather than hard coding stuff	
		if (surface->properties.roughness > 0) N = 8;
		for (j=0; j<N; j++) {
			reflection_ray.origin = nearest_intersection;
			perturb_vector(&normal, surface->properties.roughness, &rough_normal);
			reflect_vector(&ray->direction, &rough_normal, &reflection_ray.direction);
			ret = cast_ray(&reflection_ray, scene, depth-1);
			if (ret) {
				for (i=0; i<4; i++) ray->color[i] += reflection_ray.color[i] * surface->properties.reflectance / (float) N;
			}
		}
	}

	// Send refraction rays

	// By Metropolis:
	// - Difussion 
	// - Scattering

	
	return (ray);
}
