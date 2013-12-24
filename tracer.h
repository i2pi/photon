#ifndef __TRACER_H__
#define __TRACER_H__

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

typedef struct {
	int		objects;
	objectT **object;	// TODO: single deref
	int		allocated;
} sceneT;


/*
** SCENE MANAGEMENT
*/
void 	init_primitives (void);
objectT *create_cube_object (float x, float y, float z, float d);
objectT *create_sphere_object (float x, float y, float z, float d);
sceneT *create_scene (void);
void    add_object_to_scene (sceneT *s, objectT *o);

/*
** VECTOR OPS
*/

void 	normalize_vector (vectorT *v);

/*
** RAY CASTING
*/

rayT    *cast_ray (rayT *ray, sceneT *scene, int depth);

#endif 
