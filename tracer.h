#ifndef __TRACER_H__
#define __TRACER_H__

#include "gl.h" // For GLenum

typedef struct {
	float	x, y, z;
} vectorT;


typedef struct {
	vectorT		origin;
	vectorT		direction;
	float		color[4];	//RGBA
} rayT;

typedef struct {
	char	*name;
	int		parameters;
	char	**parameter_name;

	// Methods:
	// - ray-primitive intersection 

	void	(*gl_draw)(float *parameter);
	char	(*ray_intersects)(float *parameter, rayT *ray, vectorT *intersection);
} primitiveT;

typedef struct {
	// Function pointers:
	//  reflection (by wavelenth)
	//  refraction (by wavelenth)
	//  transparency (by wavelength)
	//  diffusion (by angle)
} surface_propertiesT;

typedef struct {
	// A surface is an instance of a primitve
	primitiveT *primitive;
	float		*parameter;
	float		color[4];
	surface_propertiesT *properties;
} surfaceT;

typedef struct {
	int			surfaces;
	surfaceT	*surface;
} objectT;

typedef struct lightT {
	// TODO: we'll just do positional lights for now
	vectorT	position;
	float	color[4];

	GLenum	GL_LIGHT;
	void	(*gl_draw)(struct lightT *self);
} lightT;

typedef struct {
	int		objects;
	objectT **object;	
	int		object_array_size;

	int		lights;
	lightT	**light;
	int		light_array_size;
} sceneT;


/*
** SCENE MANAGEMENT
*/
void 	init_primitives (void);
objectT *create_object (int surfaces);
objectT *create_cube_object (float x, float y, float z, float d);
objectT *create_sphere_object (float x, float y, float z, float d);
objectT *create_checkerboard_object (float y, float width, int n);
lightT	*create_positional_light (float x, float y, float z, float color[4]);
sceneT *create_scene (void);
void    add_object_to_scene (sceneT *s, objectT *o);
void    add_light_to_scene (sceneT *s, lightT *o);

/* 
** SURFACE PROPERTIES
*/
void color_object (objectT *obj, float *color);

/*
** VECTOR OPS
*/

void array_to_vector(float *arr, vectorT *v);
void vector_to_array(vectorT *v, float *arr);
void parms_to_array(float x, float y, float z, float *arr);
void parms_to_vector(float x, float y, float z, vectorT *v);
void triangle_to_array (vectorT v1, vectorT v2, vectorT v3, float *triangle);
float length_vector (vectorT *v);
void normalize_vector (vectorT *v);
void diff_vector (vectorT *a, vectorT *b, vectorT *v);
float dist_vector (vectorT *a, vectorT *b);
float	dot_vector (vectorT *a, vectorT *b);
void cross_vector(vectorT *a, vectorT *b, vectorT *v);
void project_vector (vectorT *a, vectorT *b, vectorT *v);
void scale_offset_vector (vectorT *a, vectorT *d, float s, vectorT *v);
void triangle_normal (vectorT *a, vectorT *b, vectorT *c, vectorT *n);

/*
** RAY CASTING
*/

rayT    *cast_ray (rayT *ray, sceneT *scene, int depth);

#endif 
