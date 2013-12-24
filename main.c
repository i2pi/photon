#include <unistd.h> 
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "gl.h"
#include "tracer.h"

#define ESCAPE 27

sceneT	*setup_scene (void) {
	sceneT	*s;
	int	i, j, N;
	float r;

	s = create_scene ();

	N = 7;
	r = 0.1;
	for (i=0; i<N; i++)
	for (j=0; j<N; j++) {
		float   x, y, z;
		objectT	*obj;
		x = (2.0f * i / (float) (N-1)) - 1.0;
		z = (2.0f * j / (float) (N-1)) - 1.0;
		y = -z;
		z = (z - 1.5) * 5.0;
		if ((i + j) % 2) {
			obj = create_cube_object(x, y, z, r);
		} else {
			obj = create_sphere_object(x, y, z, r);
		}
		add_object_to_scene (s, obj);
	}

	return (s);
}

int main(int argc, char **argv)
{  
	sceneT	*s;
	int W = 256, H = 256;

	init_primitives();
	s = setup_scene();

	init_gl (W, H, argc, argv, s);
  
	glutMainLoop();  

	return (1);
}
