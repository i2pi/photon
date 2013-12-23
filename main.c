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

int main(int argc, char **argv)
{  
	int W = 256, H = 256;

	init_primitives();
	init_gl (W, H, argc, argv);
  
	glutMainLoop();  

	return (1);
}
