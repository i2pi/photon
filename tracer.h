#ifndef __TRACER_H__
#define __TRACER_H__

#ifndef NO_GL
#include "gl.h" // For GLenum
#endif

#define MAX_LENSES 1024

typedef struct {
	float	x, y, z;
} vectorT;

typedef struct {
	vectorT		origin;
	vectorT		direction;
	float		color[4];	//RGBA
	float		refractive_index;
} rayT;

typedef struct {
	char	*name;
	int		parameters;
	char	**parameter_name;

	void	(*gl_draw)(float *parameter);
	char	(*ray_intersects)(float *parameter, rayT *ray, vectorT *intersection);
	void	(*normal)(float *parameter, vectorT *point, vectorT *normal);
} primitiveT;

typedef struct {
	float	color[4];
	float	reflectance;
	float	roughness;
	float	transparency;
	float	refractive_index;
} surface_propertiesT;

typedef struct surfaceT {
	// A surface is an instance of a primitve
	primitiveT *primitive;
	float		*parameter;
	surface_propertiesT properties;
	// TODO: wavelength dependance
	void	(*property_function)(struct surfaceT *self, rayT *camera_ray, vectorT *intersection, surface_propertiesT *result);
} surfaceT;

typedef struct {
	int			surfaces;
	surfaceT	*surface;
} objectT;

typedef struct lightT {
	// TODO: we'll just do positional lights for now
	vectorT	position;
	float	color[4];

#ifndef NO_GL
	GLenum	GL_LIGHT;
#endif
	void	(*gl_draw)(struct lightT *self);
} lightT;

typedef struct {
	// r1 & r2 are the curvature radii for the front & back surfaces.
	// Front is defined by facing the +ve z direction.
	// The radius is the radius of the lens :P
	// Only doing spherical lenses for now.

	float	z;
	float	r1, r2;		
	float	refractive_index;
	float	radius;
	objectT	*object;
} lensT;

typedef struct {
	// Sensor is a d x d square at (0, 0, z)
	// All elements are perpendicular to the z-axis
	// Sensor is an d x d square
	// TODO: Apertures

	float	d;
	float	z;
	lensT	lens[MAX_LENSES];
	int		lenses;
} cameraT;

typedef struct {
	cameraT	camera;

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
objectT *create_lens_object (float z, float r1, float r2, float R);
objectT *create_checkerboard_object (float y, float width, int n);
objectT *create_ortho_plane_object (float nx, float ny, float nz, float pos);
lightT	*create_positional_light (float x, float y, float z, float color[4]);
sceneT *create_scene (void);
void    add_object_to_scene (sceneT *s, objectT *o);
void    add_light_to_scene (sceneT *s, lightT *o);
void 	add_lens_to_camera (cameraT *camera, 
			float z, float r1, float r2, float R, float refractive_index);

/* 
** SURFACE PROPERTIES
*/
void color_object (objectT *obj, float *color, 
			float reflectance, 
			float roughness,
			float transparency,
			float refractive_index);

void set_object_property_function (objectT *obj, void    (*property_function)(struct surfaceT *self, rayT *camera_ray, vectorT *intersection, surface_propertiesT *result));

void    checker_property_function (surfaceT *self, rayT *camera_ray, vectorT *intersection, surface_propertiesT *result);

/*
** VECTOR OPS
*/

void array_to_vector(float *arr, vectorT *v);
void vector_to_array(vectorT *v, float *arr);
void params_to_array(float x, float y, float z, float *arr);
void params_to_vector(float x, float y, float z, vectorT *v);
void triangle_to_array (vectorT v1, vectorT v2, vectorT v3, float *triangle);
float length_vector (vectorT *v);
void normalize_vector (vectorT *v);
void diff_vector (vectorT *a, vectorT *b, vectorT *v);
float dist_vector (vectorT *a, vectorT *b);
float	dot_vector (vectorT *a, vectorT *b);
float   cosine_vector (vectorT *a, vectorT *b);
void cross_vector(vectorT *a, vectorT *b, vectorT *v);
void project_vector (vectorT *a, vectorT *b, vectorT *v);
void reflect_vector (vectorT *v, vectorT *n, vectorT *r);
char refract_vector(vectorT *v, vectorT *n, float n1, float n2, vectorT *r);
void scale_offset_vector (vectorT *a, vectorT *d, float s, vectorT *v);
void triangle_normal_vector (vectorT *a, vectorT *b, vectorT *c, vectorT *n);

/*
** RAY CASTING
*/

rayT    *cast_ray (rayT *ray, sceneT *scene, int depth);
char    cast_ray_through_camera(rayT *ray, cameraT *camera, rayT *out);

#endif 
