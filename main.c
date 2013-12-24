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

void	render_scene(void)
{
	int		i, j;

	// Render GL version
	set_camera();
	glViewport(0, 0, gui_state.w/2, gui_state.h);		

	glClearColor(1,1,1,1);
	glClearDepth(1);				
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

	for (i=0; i<SCENE->objects; i++) {
		objectT *obj = SCENE->object[i];
		glColor4f (1, 0, 0, 1);
		for (j=0; j<obj->primitives; j++) {
			obj->primitive[j]->gl_draw(obj->parameter[j]);
		}
	}

	// Render Ray traced version
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
	glViewport(gui_state.w/2, 0, gui_state.w/2, gui_state.h);		

	draw_pixels_to_texture(SCREEN_PIXELS, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_TEXTURE_ID);

	glBindTexture(GL_TEXTURE_2D, SCREEN_TEXTURE_ID);
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

void init_screen(void) {
	int	x, y;
	int	i;

	init_texture_for_pixels(SCREEN_TEXTURE_ID);	
	SCREEN_PIXELS = (char *) malloc (sizeof(char) * SCREEN_WIDTH * SCREEN_HEIGHT * 4); // 3?

	i = 0;
	for (y=0; y<SCREEN_HEIGHT; y++)
	for (x=0; x<SCREEN_WIDTH; x++)
	{
		SCREEN_PIXELS[i++] = (x&0x3F) + (y&0x70);
		SCREEN_PIXELS[i++] = (x&0xf0) + (y&0x0f);
		SCREEN_PIXELS[i++] = (x&0x0f) + (y&0xf0);
	}
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
