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

#define SCREEN_TEXTURE_ID	1
#define SCREEN_WIDTH		256
#define SCREEN_HEIGHT		256

sceneT	*SCENE;

char	*SCREEN_PIXELS;

void	ray_trace_to_pixels (sceneT *scene, int width, int height, char *pixels) {
	int	x, y;
	
	vectorT	camera;

	camera.x = 0;
	camera.y = 0;
	camera.z = 0;

	for (y=0; y<height; y++) 
	for (x=0; x<width; x++) {
		rayT	ray;
		rayT 	*ret;

		ray.origin = camera;

		ray.direction.x = 1.0 - 2.0*(x / (float) width);
		ray.direction.y = 1.0 - 2.0*(y / (float) height);
		ray.direction.z = 2.445; // TODO: calc from fov
		normalize_vector(&ray.direction);

		memset (&ray.color[0], 0, sizeof(float) * 4);
		ret = cast_ray (&ray, SCENE, 5);
		if (ret) {
			pixels[((y*width) + x)*3 + 0] = ret->color[0] * 255;
			pixels[((y*width) + x)*3 + 1] = ret->color[1] * 255;
			pixels[((y*width) + x)*3 + 2] = ret->color[2] * 255;
		}
	}
}

void	render_scene(void)
{
	int		i, j;

	// Render GL version

// TODO: Move lighting definitions to tracer.h
typedef struct
{
  float pos[4];
  float diffuse[4];
  float specular[4];
  float ambient[4];
}light_t;
light_t light={
      {0,0,-15,0},  //position (the final 1 means the light is positional)
      {1,1,1,1},    //diffuse
      {0,0,0,1},    //specular
      {0,0,0,1} 	//ambient
    };

glLightfv(GL_LIGHT0,GL_POSITION,light.pos);   	//updates the light's position
glLightfv(GL_LIGHT0,GL_DIFFUSE,light.diffuse);    //updates the light's diffuse colour
glLightfv(GL_LIGHT0,GL_SPECULAR,light.specular);  //updates the light's specular colour
glLightfv(GL_LIGHT0,GL_AMBIENT,light.ambient);    //updates the light's ambient colour


	set_camera();
	glViewport(0, 0, gui_state.w/2, gui_state.h);		

	glClearColor(1,1,1,1);
	glClearDepth(1);				
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	for (i=0; i<SCENE->objects; i++) {
		objectT *obj = SCENE->object[i];

		for (j=0; j<obj->surfaces; j++) {
			surfaceT *surf = &obj->surface[j];

			// TODO: Make a gl_surface_properties() and put it in gl_draw()
			glColor4fv (surf->color);
			glColorMaterial(GL_FRONT,GL_DIFFUSE);	
			float black[4]={0,0,0,0};
			glMaterialfv(GL_FRONT,GL_AMBIENT,black);
			glMaterialfv(GL_FRONT,GL_SPECULAR,black);

			surf->primitive->gl_draw(surf->parameter);
		}
	}

	// Render Ray traced version
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
	glViewport(gui_state.w/2, 0, gui_state.w/2, gui_state.h);		

	//ray_trace_to_pixels(SCENE, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_PIXELS);

	draw_pixels_to_texture(SCREEN_PIXELS, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_TEXTURE_ID);

	glBindTexture(GL_TEXTURE_2D, SCREEN_TEXTURE_ID);
	glColor4f(0,0,0,1);
	glBegin(GL_QUADS);
	glTexCoord2f(0,0);
	glVertex2f  (-1,-1);
	glTexCoord2f(1,0);
	glVertex2f  ( 1,-1);
	glTexCoord2f(1,1);
	glVertex2f  ( 1, 1);
	glTexCoord2f(0,1);
	glVertex2f  (-1, 1);
	glEnd();
	glBindTexture(GL_TEXTURE_2D, 0); // Unbind any textures
	
	glutSwapBuffers();	
}

sceneT	*setup_scene (void) {
	sceneT	*s;
	objectT	*obj;
	int	i, j, N;
	float r;
	float red[4] =   {1, 0, 0, 1};
	float green[4] = {0, 1, 0, 1};

	s = create_scene ();

/*
	N = 7;
	r = 0.1;
	for (i=0; i<N; i++)
	for (j=0; j<N; j++) {
		float   x, y, z;
		x = (2.0f * i / (float) (N-1)) - 1.0;
		z = (2.0f * j / (float) (N-1)) - 1.0;
		y = -z;
		z = (z - 1.5) * 5.0;
		if ((i + j) % 2) {
			obj = create_cube_object(x, y, z, r);
			color_object (obj, red);
		} else {
			obj = create_sphere_object(x, y, z, r);
			color_object (obj, green);
		}
		add_object_to_scene (s, obj);
	}
*/
	obj = create_cube_object(0, 0, -3, 0.5);
	color_object(obj, red);
	add_object_to_scene (s, obj);

	return (s);
}

void init_screen(void) {
	init_texture_for_pixels(SCREEN_TEXTURE_ID);	
	SCREEN_PIXELS = (char *) calloc (sizeof(char), SCREEN_WIDTH * SCREEN_HEIGHT * 3);
}

int main(int argc, char **argv)
{  
	init_primitives();

	SCENE = setup_scene();

	init_gl (argc, argv);

	glEnable (GL_TEXTURE_2D);

	init_screen ();
  
	glutMainLoop();  

	return (1);
}
