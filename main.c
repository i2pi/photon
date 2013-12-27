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
#define SCREEN_WIDTH		512
#define SCREEN_HEIGHT		512

sceneT	*SCENE;

char	*SCREEN_PIXELS;

float	clamp (float x) {
	if (x < 1) return (x);
	return (1);
}

#ifdef WIREFRAME
	#define MAX_RAY_DISPLAY_BUFFER 256
	rayT ray_display_buffer[MAX_RAY_DISPLAY_BUFFER];
	int	 ray_display_buffers = 0;

	void add_ray_to_display_buffer (rayT *ray) {
		int i;
		i = ray_display_buffers++;
		if (i >= MAX_RAY_DISPLAY_BUFFER) {
			i = 0;
			ray_display_buffers = i;
		}
		ray_display_buffer[i] = *ray;
	}

	void display_ray_buffer(void) {
		int i;
		for (i=0; i<ray_display_buffers; i++) {
			rayT *r = &ray_display_buffer[i];
			gl_show_ray(r->origin.x, r->origin.y, r->origin.z,
					    r->direction.x, r->direction.y, r->direction.z);
		}
	}
#endif



char single_ray_trace_to_pixels (sceneT *scene, int width, int height, int x, int y, char *pixels) {
	rayT	ray;
	rayT 	*ret;

	ray.origin = scene->camera;

	ray.direction.x = 2.0*(x / (float) width) - 1.0;
	ray.direction.y = 2.0*(y / (float) height) - 1.0;
	ray.direction.z = -2.445; // TODO: calc from fov

	normalize_vector(&ray.direction);

#ifdef WIREFRAME 
	add_ray_to_display_buffer(&ray);
#endif

	memset (&ray.color[0], 0, sizeof(float) * 4);
	ret = cast_ray (&ray, SCENE, 3);
	if (ret) {
		pixels[((y*width) + x)*3 + 0] = clamp(ret->color[0]) * 255;
		pixels[((y*width) + x)*3 + 1] = clamp(ret->color[1]) * 255;
		pixels[((y*width) + x)*3 + 2] = clamp(ret->color[2]) * 255;
		return (1);
	}
	return (0);	
}

void	ray_trace_to_pixels (sceneT *scene, int width, int height, char *pixels) {
	int	x, y;
	
	for (y=0; y<height; y++) 
	for (x=0; x<width; x++) {
		single_ray_trace_to_pixels (scene, width, height, x, y, pixels);
	}
}

void	render_scene(void)
{
	int		i, j;
	static unsigned long frame = 0;
	unsigned char key;

	key = get_last_key();
	frame ++;

	// Render GL version
//	set_camera();
	float   fov = 45.0f;
    float   aspect = 1.0f;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(fov,aspect,0.1f,100.0f);
	glTranslatef(0,0,-2);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
	glRotatef(frame*0.01, 0,1,0);

	glViewport(0, 0, gui_state.w/2, gui_state.h);		

	glClearColor(0,0,0,0);
	glClearDepth(1);				
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

#ifdef WIREFRAME
	// Show the camera as axes markers
	gl_axes_wireframe(SCENE->camera.x, SCENE->camera.y, SCENE->camera.z);

	display_ray_buffer();
#endif	

	for (i=0; i<SCENE->lights; i++) {
		SCENE->light[i]->gl_draw(SCENE->light[i]);
	}

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
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
	glViewport(gui_state.w/2, 0, gui_state.w/2, gui_state.h);		

	if (key == 's') {
		printf ("Starting render... ");
		ray_trace_to_pixels(SCENE, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_PIXELS);
		printf ("Render complete!\n");
	} else 
	if (key == ' ') {
		int x, y;
		char hit;

		x = random() % SCREEN_WIDTH;
		y = random() % SCREEN_HEIGHT;
		hit = single_ray_trace_to_pixels(SCENE, SCREEN_WIDTH, SCREEN_HEIGHT, x, y, SCREEN_PIXELS);
		printf ("(%3d,%3d) -> %s\n", x, y, hit ? "Hit" : "miss");
		
	}

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
	lightT	*l;
	objectT	*obj;
	float r;
	float green[4] = {0, 1, 0, 1};
	float white[4] = {1, 1, 1, 1};
	float sky[4] = {0.4, 0.8, 0.95, 1};
	float pink[4] = {1, 0.2, 0.2, 1};

	s = create_scene ();

	r = 0.3;

	obj = create_sphere_object(0, 0, 0, r);
	color_object (obj, white, 0.2);
	add_object_to_scene (s, obj);

	obj = create_sphere_object(-r/2.0, -r/3.0, 0.5, r/3.0);
	color_object (obj, pink,0.5);
	add_object_to_scene (s, obj);

	obj = create_sphere_object(0.5, -r/3.0, -0.5, r/3.0);
	color_object (obj, sky,0.5);
	add_object_to_scene (s, obj);

	obj = create_checkerboard_object(-r*0.75, 1, 20);
	add_object_to_scene (s, obj);

	l = create_positional_light(3,2,3, green);
	add_light_to_scene (s, l);

	l = create_positional_light(0,5,0, white);
	add_light_to_scene (s, l);

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
