#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <string.h>

#include "gl.h"
#include "tracer.h"
#include "wireframe.h"
#ifdef USE_METAL
#include "metal_tracer.h"
#else
#include "cpu_render.h"
#endif

#define ESCAPE 27

#define SCREEN_TEXTURE_ID	1

#define SCREEN_WIDTH	  1920
#define SCREEN_HEIGHT	  1080

#define MIN_SAMPLES   128
#define MAX_SAMPLES    1000
#define QUAL_THRESH   0.1
#define TRACE_DEPTH   8

#define PI 3.1415926535

sceneT	*SCENE;

char	*SCREEN_PIXELS;
float   *SCREEN_PIXELS_F;

sceneT	*setup_scene (int idx) {
  sceneT	*s;
  lightT	*l;
  objectT	*obj;
  int		i;
  //float sea[4] = {0.15, 0.92, 0.66, 1};
  float sky[4] = {0.5, 0.75, 0.75, 1};
  float white[4] = {0.95, 0.97, 0.96, 1};
  float pink[4] = {0.99, 0.58, 0.62, 1};
  //float orange[4] = {1,0.7,0.4,1};

  float xos, yos, zos; // offsets 

  xos = yos = zos = 0.0f;
  xos = -0.30;
  zos = -0.3;


  s = create_scene ();

  obj = create_sphere_object(0.0+xos, 0.0+yos, -2.75+zos, 0.9);
  color_object (obj, pink, 0.0,1.0, 0.0, 1.0, 0.0);
  add_object_to_scene (s, obj);

  for (i=0; i<6; i++) {
    float base_ri = 1.1 + i*0.09*sin(PI * idx / 48.0);
    obj = create_sphere_object(0.41*sin(PI * (idx/32.0) + i*0.20) + xos, 0.0+yos, -2.75+zos, 1.04+pow(i,1.155)*(0.17 + 0.17*sin(i*0.98 + PI * idx/128.0)));
    color_object (obj, sky, 0.0,0.0, 0.97 -  i*0.025, base_ri, 0.04);

    add_object_to_scene (s, obj);
  }

  obj = create_ortho_plane_object(0, 0, 1, -800);
  color_object (obj, white, 0.2,0.5, 0.0, 1.0, 0.0);
  add_object_to_scene (s, obj);

  float lens_base_ri = 0.94 + 0.2 * sin(PI * (idx +17) / 192.0);
  add_lens_to_camera(&s->camera, 0, 1.7,2.8 + 0.7*sin((32+idx) / 64.0), 0.1, lens_base_ri, 0.06);
  add_object_to_scene(s, s->camera.lens[0].object);


  float color[4];
  color[0] = 0.8;
  color[1] = color[0];
  color[2] = color[0];
  color[3] = color[0];
  l = create_positional_light(-1,7,5, color);
  add_light_to_scene (s, l);

  l = create_positional_light(7,-5,4, color);
  add_light_to_scene (s, l);

  color[0] = 0.7;// / (i + 1.4);
  color[1] = color[0];
  color[2] = color[0];
  color[3] = color[0];
  l = create_positional_light(-7,-5,1, color);
  add_light_to_scene (s, l);

  return (s);
}

//unsigned long best_frame[] = {20, 21, 112, 116, 208, 314, 389, 387, 574, 1089, 1290};
unsigned long best_frame[] = {20,112,574};

void	render_scene(void)
{
	int		i, j;
	static unsigned long frame = 0;
	static char pause_rays = 0;
	//static int x = 0, y = 0;
	unsigned char key;

    if (frame >= 1) exit(0);
  SCENE = setup_scene(best_frame[frame % 11]); // fuck it, lets just leak mem for now



	key = get_last_key();
	frame ++;

	// Render GL version
//	set_camera();
	float   fov = 45.0f;
    float   aspect = 1.0f;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(fov,aspect,0.1f,100.0f);
	glTranslatef(0,0,-10);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (!pause_rays) glRotatef(frame*0.5, 0,1,0);

	glViewport(0, 0, gui_state.w/2, gui_state.h);		

	glClearColor(0,0,0,0);
	glClearDepth(1);				
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

#ifdef WIREFRAME
	// Show the camera as axes markers
	gl_axes_wireframe(0, 0, SCENE->camera.z);

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
			glColor4fv (surf->properties.color);
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

	switch (key) {
		case 'p': pause_rays = pause_rays ? 0 : 1; break;
	}

//  SCENE->camera.z = 0 + frame / 300.0f;
//  SCENE->camera.d = 0.5 + sin(frame / 17.0);

  struct timeval start, end;
	float elapsed;

  gettimeofday(&start, NULL);
#ifdef USE_METAL
	gpu_ray_trace_to_pixels(SCENE, SCREEN_WIDTH, SCREEN_HEIGHT,
		MIN_SAMPLES, MAX_SAMPLES, QUAL_THRESH, TRACE_DEPTH,
		SCREEN_PIXELS, SCREEN_PIXELS_F);
#else
	ray_trace_to_pixels(SCENE, SCREEN_WIDTH, SCREEN_HEIGHT,
		MIN_SAMPLES, MAX_SAMPLES, QUAL_THRESH, TRACE_DEPTH,
		SCREEN_PIXELS, SCREEN_PIXELS_F);
#endif
	save_screen(frame, SCREEN_PIXELS, SCREEN_WIDTH, SCREEN_HEIGHT);
	save_screen_f(frame, SCREEN_PIXELS_F, SCREEN_WIDTH, SCREEN_HEIGHT);

  gettimeofday(&end, NULL);
	elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6f;

  printf ("[%04lu : %4.2f] %6.4fs  %6.4ffps\n", frame, frame/24.0, elapsed, 1.0 / elapsed);


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

void init_screen(void) {
	init_texture_for_pixels(SCREEN_TEXTURE_ID);	
	SCREEN_PIXELS = (char *) calloc (sizeof(char), SCREEN_WIDTH * SCREEN_HEIGHT * 3);
	SCREEN_PIXELS_F = (float *) calloc (sizeof(float), SCREEN_WIDTH * SCREEN_HEIGHT * 3);
}

int main(int argc, char **argv)
{  
	init_primitives();
	init_spectral();

#ifdef USE_METAL
	if (gpu_init() != 0) {
		fprintf(stderr, "Metal init failed, falling back to CPU\n");
	}
#endif

	init_gl (argc, argv);

	init_screen ();
  
	glutMainLoop();  

	return (1);
}
