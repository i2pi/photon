#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include "tracer.h"
#include "gl.h"
#include "wireframe.h"

#undef DEBUG

primitiveT	*SPHERE;
primitiveT	*TRIANGLE;
primitiveT	*ORTHO_PLANE;
primitiveT	*LENS;

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

char refract_vector(vectorT *v, vectorT *n, float n1, float n2, vectorT *r) {
	// r = refraction of v against normal n on boundary of n1:n2

	// http://steve.hollasch.net/cgindex/render/refraction.txt
	
   //Vector3  I, N, T;		/* incoming, normal and Transmitted */
	double eta, c1, cs2 ;

	eta = n1 / n2 ;			
	c1 = -dot_vector(v, n);
	cs2 = 1.0 - pow(eta, 2.0) * (1 - pow(c1, 2.0));

	if (cs2 < 0)
		return (0);		// total internal reflection 
	
	r->x = eta * v->x + (eta * c1 - sqrt(cs2)) * n->x;
	r->y = eta * v->y + (eta * c1 - sqrt(cs2)) * n->y;
	r->z = eta * v->z + (eta * c1 - sqrt(cs2)) * n->z;

	return (1);
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

void lens_gl_draw (float *parameter) {
	float	z, r1, r2, R;

	z = parameter[0];
	r1 = parameter[1];
	r2 = parameter[2];
	R = parameter[3];

	gl_xy_sphere_cap (r1, R, z, 15);
	gl_xy_sphere_cap (-r2, R, z, 15);
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

void ortho_plane_gl_draw(float *parameter) {
	gl_ortho_plane (parameter[0], parameter[1], parameter[2], parameter[3], 5, 40);
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
	surf->property_function = NULL;
	surf->properties.reflectance = 0.0f;
	surf->properties.roughness = 0.0f;
	surf->properties.transparency = 0.0f;
	surf->properties.refractive_index = 1.0f;
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

objectT *create_lens_object (float z, float r1, float r2, float R) {
	objectT 	*obj;
	surfaceT	*surf;
	
	obj = create_object(1);	
	surf = &obj->surface[0];
	init_surface(LENS, surf);	
	surf->parameter[0] = z;
	surf->parameter[1] = r1;
	surf->parameter[2] = r2;
	surf->parameter[3] = R;

	return (obj);
}

objectT *create_ortho_plane_object (float nx, float ny, float nz, float pos) {
	objectT *obj;
	surfaceT *surf;	

	obj = create_object(1);
	
	surf = &obj->surface[0];	
	init_surface (ORTHO_PLANE, surf);
	surf->parameter[0] = nx;
	surf->parameter[1] = ny;
	surf->parameter[2] = nz;
	surf->parameter[3] = pos;

	return (obj);
}

void	checker_property_function (surfaceT *self, rayT *camera_ray, vectorT *intersection, surface_propertiesT *result) {
	// produces a checkerboard in the xz plane

	float scale = 2.0f;

	result->color[0] = 1.0;
	result->color[1] = 1.0;
	result->color[2] = 1.0;
	result->color[3] = 1.0;

	result->reflectance = 0.2;
	result->roughness = 0.0;
	result->transparency = 0.0;
	result->refractive_index = 1.0;

	int a = (int)(5000+(intersection->x * scale)) + (int)(5000+(intersection->z * scale));

	if (a & 1) {
		result->color[0] = 0.0;
		result->color[1] = 0.0;
		result->color[2] = 0.0;

		result->reflectance = 0.95;
	}
}


char	null_ray_intersects (float *parameter, rayT *ray, vectorT *intersection) {
	return (0);
}

void	sphere_normal(float *parameter, vectorT *point, vectorT *normal) {
	vectorT center;
	array_to_vector(parameter, &center);
	diff_vector(point, &center, normal);
	normalize_vector(normal);
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

			// epsilon hack :/ 
			if ((vpc_len + 0.00001) >= radius) {
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

char	ray_through_lens (rayT *ray, lensT *lens, rayT *out) {
	float	sphere_parameter[4];
	vectorT	normal;
	vectorT	intersection;
	char	hit;
	float 	dist;

	float	z, r1, r2, R;

	z = lens->z;
	r1 = lens->r1;
	r2 = lens->r2;
	R = lens->radius;

	// Coming in to the front of the lens
	if (ray->origin.z < z) return(0);
	if (ray->direction.z > 0) return (0);	// Wong ray.

	sphere_parameter[0] = 0; 	// center.x
	sphere_parameter[1] = 0;	// center.y
	sphere_parameter[2] = z - sqrt(powf(r1, 2.0f) - powf(R,2.0f));
	sphere_parameter[3] = r1;

	hit = ray_sphere_intersection (sphere_parameter, ray, &intersection);
	if (!hit) return (0);

	// Check tht the intersection is on the cap
	dist = sqrt(powf(intersection.x, 2.0f) + powf(intersection.y, 2.0f));
	if (dist > R) return (0);

	// Refract
	out->origin = intersection;
	sphere_normal(sphere_parameter, &intersection, &normal);
	refract_vector(&ray->direction, &normal, 1, lens->refractive_index, &out->direction);

	
	// Pass the light through the back of the lens	

	sphere_parameter[0] = 0; 	// center.x
	sphere_parameter[1] = 0;	// center.y
	sphere_parameter[2] = z + sqrt(powf(r2, 2.0f) - powf(R,2.0f));
	sphere_parameter[3] = r2;

	hit = ray_sphere_intersection (sphere_parameter, out, &intersection);
	if (!hit)  { 
    // TODO - this happens often!
		printf ("ODD\n");
		return (0);
	}

	out->origin = intersection;
	sphere_normal(sphere_parameter, &intersection, &normal);
	normal.x *= -1.0;
	normal.y *= -1.0;
	normal.z *= -1.0;
	refract_vector(&ray->direction, &normal, lens->refractive_index, 1, &out->direction);

  out->refractive_index = 1.0;

	return (1);
}

char	ray_lens_intersection (float *parameter, rayT *ray, vectorT *intersection) {
	return (0);
}

char	cast_ray_through_camera(rayT *ray, cameraT *camera, rayT *out) {
	int		i;
	char	hit;

	for (i=0; i<camera->lenses; i++) {
		hit = ray_through_lens (ray, &camera->lens[i], out);
		add_seg_to_display_buffer(&ray->origin, &out->origin, 1, 0, 0);
		// TODO: lens barrel
		if (!hit) return (0);
	}

	return (1);
}

void add_lens_to_camera (cameraT *camera, float z, float r1, float r2, float R, float refractive_index) {
	int	i;
		
	i = camera->lenses;
	camera->lens[i].z = z;
	camera->lens[i].r1 = r1;
	camera->lens[i].r2 = r2;
	camera->lens[i].radius = R;
	camera->lens[i].refractive_index = refractive_index;
	camera->lens[i].object = create_lens_object(z, r1, r2, R);
	camera->lenses++;
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

char	ray_ortho_plane_intersection (float *parameter, rayT *ray, vectorT *intersection) {
	vectorT normal;
	vectorT pos;
	float d;

	array_to_vector(&parameter[0], &normal);

	pos = normal;
	scale_vector(&pos, parameter[3]);

	d = dot_vector(&normal, &ray->direction);

	if (d >= 0) {	
		return (0);
	}
	
	if (normal.x != 0) {
		d = (pos.x - ray->origin.x) / ray->direction.x;
	} else 
	if (normal.y != 0) {
		d = (pos.y - ray->origin.y) / ray->direction.y;
	} else {
		d = (pos.z - ray->origin.z) / ray->direction.z;
	}

	scale_offset_vector(&ray->origin, &ray->direction, d, intersection);

	return (1);
}



void color_object (objectT *obj, float *color, 
		float reflectance, 
		float roughness,
		float transparency,
		float refractive_index) {
	int	i;

	for (i=0; i<obj->surfaces; i++) {
		memcpy(obj->surface[i].properties.color, color, sizeof(float) * 4);
		obj->surface[i].properties.reflectance = reflectance;
		obj->surface[i].properties.roughness = roughness;
		obj->surface[i].properties.transparency = transparency;
		obj->surface[i].properties.refractive_index = refractive_index;
	}
}

void set_object_property_function (objectT *obj,
		void    (*property_function)(struct surfaceT *self, rayT *camera_ray, vectorT *intersection, surface_propertiesT *result)) {
	int	i;

	for (i=0; i<obj->surfaces; i++) {
		obj->surface[i].property_function = property_function;
	}
}



void	triangle_normal(float *parameter, vectorT *point, vectorT *normal) {
	array_to_vector(&parameter[9], normal);
}

void	ortho_plane_normal(float *parameter, vectorT *point, vectorT *normal) {
	array_to_vector(&parameter[0], normal);
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

	ORTHO_PLANE = create_primitive("ortho_plane", ortho_plane_gl_draw, ray_ortho_plane_intersection, ortho_plane_normal,
							4, "nx", "ny", "nz", "pos");

	LENS = create_primitive("lens", lens_gl_draw, ray_lens_intersection, sphere_normal,
							4, "z", "r1", "r2", "R");

	// todo: lens, aperture, tube
}

sceneT *create_scene (void) {
	sceneT 	*s;

	s = (sceneT *) malloc (sizeof(sceneT));
	s->object_array_size = 1024;
	s->objects = 0;
	s->object = (objectT **) malloc (sizeof(objectT *) * s->object_array_size);

	s->light_array_size = 64;
	s->lights = 0;
	s->light = (lightT **) malloc (sizeof(lightT *) * s->light_array_size);

	s->camera.d = 0.75;
	s->camera.z = 1.1;
	s->camera.lenses = 0;

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
	if (s->lights < 8) l->GL_LIGHT = gl_lights[s->lights];

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
			ref = 0.1;
		} else {
			color[0] = 0.0;	
			color[1] = 0.0;	
			color[2] = 0.0;
			color[3] = 1.0;	
			ref = 0.3;
		}

		x = 2.0*width*((i / (float)(n-1)) - 0.5);
		z = 2.0*width*((j / (float)(n-1)) - 0.5);
		
        s = &obj->surface[(i*n + j)*2 + 0];
		params_to_array (x,y,z, &s->parameter[0]);
		params_to_array (x+w,y,z, &s->parameter[3]);
		params_to_array (x+w,y,z+w, &s->parameter[6]);
		vector_to_array (&norm, &s->parameter[9]);
		memcpy (s->properties.color, color, sizeof(float) * 4);
		s->properties.reflectance = ref;

        s = &obj->surface[(i*n + j)*2 + 1];
		params_to_array (x,y,z, &s->parameter[0]);
		params_to_array (x,y,z+w, &s->parameter[3]);
		params_to_array (x+w,y,z+w, &s->parameter[6]);
		vector_to_array (&norm, &s->parameter[9]);
		memcpy (s->properties.color, color, sizeof(float) * 4);
		s->properties.reflectance = ref;
    }

    return (obj);
}

float line_of_sight(sceneT *scene, vectorT *a, vectorT *b) {
	// Can a see b in this scene?
  //
  // Returns -1 if they can't see eachother
  // Returns +1 if there is direct line of sight
  // Returns 0..1 if there is line of sight through a transparent object

	rayT	ray;
	int	i, j;
  //float transparency = 0.0;

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
          if (surf->properties.transparency == 0) return (-1.0);

   //       transparency += surf->properties.transparency == 0;
				}
			}
		}
	}

	return (1.0);
}

void perturb_vector (vectorT *a, float s, vectorT *p) {
	// todo: uniform sampling in a cone
	p->x = a->x + s*(1.0 - 0.5*random() / (float) RAND_MAX);	
	p->y = a->y + s*(1.0 - 0.5*random() / (float) RAND_MAX);	
	p->z = a->z + s*(1.0 - 0.5*random() / (float) RAND_MAX);	
	normalize_vector(p);
}

#ifdef DEBUG
void print_pad(int d) {
  for(;d>0;d--) {
    printf ("  ");
  }
  printf("|");
}
#endif 

rayT	*cast_ray (rayT *ray, sceneT *scene, int depth) {
  /* 
   * This is kinda the important function.
   *
   * Given a ray, find any objects in the scene that are
   * hit by that ray. And then color the light from those
   * intersections, reflections, refractions, transparencies, etc.
   *
   * This is a recursive function and it is passed an
   * initial 'depth' and will not recurse beyond that.
   */


	int	i, j;

	float	nearest_distance = 9e99;
	vectorT	nearest_intersection;
	char	found_hit = 0;

	surfaceT *surface;
	vectorT	 normal;

	surface_propertiesT properties;


#ifdef DEBUG
  print_pad(10 - depth);
  printf ("cast_ray(): \n");
#endif 

	if (depth < 0) {
    return (ray); 
  }

  // Find the nearest intersection

	for (i=0; i<scene->objects; i++) {
    // Iterate through every object in the scene
		objectT *obj = scene->object[i];

    // Iterate through each of the surfaces of that object
		for (j=0; j<obj->surfaces; j++) {
			vectorT 	intersection;
			surfaceT 	*surf = &obj->surface[j];
			primitiveT *p = surf->primitive;

			if (p == LENS) continue;

			if (p->ray_intersects(surf->parameter, ray, &intersection)) {
				// We have a hit!
				float	distance;

				distance = dist_vector(&intersection, &ray->origin);

        // distance > 0.0001 to stop ray intersecting with its origin surface
				if ((distance < nearest_distance) && (distance > 0.00001))  {
					found_hit = 1;
					nearest_distance = distance;
					nearest_intersection = intersection;
					surface = surf;		
				}
			}
		}
	}

	if (!found_hit) return(NULL);

	add_seg_to_display_buffer (&ray->origin, &nearest_intersection, 1,1,1);

	if (surface->property_function) {
		// todo: should probably get different properties for phong vs. diffuse
		surface->property_function(surface, ray, &nearest_intersection, &properties);
	} else {
		properties = surface->properties;
	}

	// Get incident light at the point of intersection on the surface

	for (i=0; i<4; i++) ray->color[i] = 0;

	surface->primitive->normal(surface->parameter, &nearest_intersection, &normal);

	for (i=0; i<scene->lights; i++) {
		lightT	*light;
		vectorT	incidence;
		vectorT reflection;
		float	distance;
		float	diffuse, specular;
		float	phong;
    float light_transparency;

		light = scene->light[i];

		light_transparency = line_of_sight(scene, &nearest_intersection, &light->position);

    if (light_transparency < 0) continue;

		add_seg_to_display_buffer (&light->position, &nearest_intersection, 0.5,0.5,0);

		diff_vector(&light->position, &nearest_intersection, &incidence);

		distance = length_vector(&incidence);
		normalize_vector(&incidence);

		diffuse = dot_vector(&normal, &incidence); 

		reflect_vector(&incidence, &normal, &reflection);
		specular = dot_vector(&reflection, &ray->direction);

		if (diffuse < 0) diffuse = 0;
		if (specular <0) specular= 0;

		// TODO: put shininess in properties
		phong = (1.0 - properties.transparency) * diffuse * 0.9f +
				powf(specular, 35.0f) * properties.reflectance;
	
		for (j=0; j<4; j++) {
			ray->color[j] += light_transparency * phong * properties.color[j] * light->color[j] / (1.0 + distance * 0.0001);
		}
	}

	// Send reflection ray

	if (properties.reflectance > 0) {
		rayT	reflection_ray, *ret;
		vectorT	rough_normal;
		int	j, N = 1;

		for (i=0; i<4; i++) reflection_ray.color[i] = 0;
	
		// TODO: better mechanism for sampling rather than hard coding stuff	
		if (properties.roughness > 0) N = 1;
		for (j=0; j<N; j++) {
			reflection_ray.origin = nearest_intersection;
			perturb_vector(&normal, properties.roughness, &rough_normal);
			reflect_vector(&ray->direction, &rough_normal, &reflection_ray.direction);
			ret = cast_ray(&reflection_ray, scene, depth-1);
			if (ret) {
				for (i=0; i<4; i++) ray->color[i] += reflection_ray.color[i] * properties.reflectance / (float) N;
			}
		}
	}

	// Send refraction rays

	if (properties.transparency > 0) {
		rayT	refraction_ray, *ret;
		vectorT	rough_normal;
		int	j, N = 1;

		for (i=0; i<4; i++) refraction_ray.color[i] = 0;
	
		// todo: better mechanism for sampling rather than hard coding stuff	
		if (properties.roughness > 0) N = 1;
		for (j=0; j<N; j++) {
			char	refracts = 0;
			refraction_ray.origin = nearest_intersection;
			perturb_vector(&normal, properties.roughness, &rough_normal);

			// work out whether the incident ray begins inside or outside of the surface
			vectorT op;
			
			diff_vector(&nearest_intersection, &ray->origin, &op);
			if (dot_vector(&op, &rough_normal) < 0) {
				// outside
        
				refraction_ray.refractive_index = properties.refractive_index;
				refracts = refract_vector(&ray->direction, &rough_normal, 
								ray->refractive_index, 
								refraction_ray.refractive_index,
								&refraction_ray.direction);
			} else {
				scale_vector(&rough_normal, -1.0);
				refraction_ray.refractive_index =1.0;
				refracts = refract_vector(&ray->direction, &rough_normal, 
								ray->refractive_index, 
								refraction_ray.refractive_index,
								&refraction_ray.direction);
			}

			if (!refracts) {
				// internal reflection
				reflect_vector(&ray->direction, &normal, &refraction_ray.direction);
			}

			ret = cast_ray(&refraction_ray, scene, depth-1);
			if (ret) {
				for (i=0; i<4; i++) ray->color[i] += refraction_ray.color[i] * properties.transparency / (float) N;
			}
	
		}
	}

	// By Metropolis:
	// - Difussion 
	// - Scattering
	
	return (ray);
}
