#ifndef __TRACER_H__
#define __TRACER_H__

typedef struct {
	float	x, y, z;
} vectorT;

typedef struct {
	vectorT		origin;
	vectorT		direction;
} rayT;

typedef struct {
	char	*name;
	int		parameters;
	char	**parameter_name;

	// Methods:
	// - ray-primitive intersection 

	void	(*gl_draw)(float *parameter);
} primitiveT;

typedef struct {
	int			primitives;
	primitiveT 	**primitive;
	float		**parameter;

	// TODO: objects become surfaces and share surface properties
	// objects then have multiple surfaces
} objectT;

typedef struct {
	int		objects;
	int		allocated;
	objectT **object;
} sceneT;


void init_primitives (void);

objectT *create_cube_object (float x, float y, float z, float d);
objectT *create_sphere_object (float x, float y, float z, float d);

sceneT *create_scene (void);
void    add_object_to_scene (sceneT *s, objectT *o);

#endif 
