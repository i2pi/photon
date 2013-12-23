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
	// - gl draw 
	
} primitiveT;

typedef struct {
	primitiveT 	*primitive;
	float		**parameter;
} primitive_instanceT;

extern primitiveT primitive[64];
extern int		   primitives;

void init_primitives (void);

#endif 
