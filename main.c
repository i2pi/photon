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
#include "scene_parser.h"
#ifdef USE_METAL
#include "metal_tracer.h"
#else
#include "cpu_render.h"
#endif

#define SCREEN_TEXTURE_ID	1

sceneT	*SCENE;

char	*SCREEN_PIXELS;
float   *SCREEN_PIXELS_F;

static render_settingsT RENDER;
static char *SCENE_FILE;

void	render_scene(void)
{
	int		i, j;
	static unsigned long frame = 0;
	static char pause_rays = 0;
	unsigned char key;

    if (frame >= 1) exit(0);

  SCENE = load_scene(SCENE_FILE, &RENDER);
  if (!SCENE) { fprintf(stderr, "Failed to load scene\n"); exit(1); }

	key = get_last_key();
	frame ++;

	// Render GL version
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

  struct timeval start, end;
	float elapsed;

  gettimeofday(&start, NULL);
#ifdef USE_METAL
	gpu_ray_trace_to_pixels(SCENE, RENDER.width, RENDER.height,
		RENDER.min_samples, RENDER.max_samples, RENDER.qual_thresh, RENDER.trace_depth,
		RENDER.shadow_rays, RENDER.ghost_rays,
		SCREEN_PIXELS, SCREEN_PIXELS_F);
#else
	ray_trace_to_pixels(SCENE, RENDER.width, RENDER.height,
		RENDER.min_samples, RENDER.max_samples, RENDER.qual_thresh, RENDER.trace_depth,
		SCREEN_PIXELS, SCREEN_PIXELS_F);
#endif
	save_screen(frame, SCREEN_PIXELS, RENDER.width, RENDER.height);
	save_screen_f(frame, SCREEN_PIXELS_F, RENDER.width, RENDER.height);

  gettimeofday(&end, NULL);
	elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6f;

  printf ("[%04lu : %4.2f] %6.4fs  %6.4ffps\n", frame, frame/24.0, elapsed, 1.0 / elapsed);


	draw_pixels_to_texture(SCREEN_PIXELS, RENDER.width, RENDER.height, SCREEN_TEXTURE_ID);

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
	glBindTexture(GL_TEXTURE_2D, 0);

	glutSwapBuffers();

}

void init_screen(void) {
	init_texture_for_pixels(SCREEN_TEXTURE_ID);
	SCREEN_PIXELS = (char *) calloc (sizeof(char), RENDER.width * RENDER.height * 3);
	SCREEN_PIXELS_F = (float *) calloc (sizeof(float), RENDER.width * RENDER.height * 3);
}

int main(int argc, char **argv)
{
	init_primitives();
	init_spectral();

	if (argc < 2 || !strstr(argv[1], ".scene")) {
		fprintf(stderr, "usage: %s <scene.scene>\n", argv[0]);
		exit(1);
	}
	SCENE_FILE = argv[1];

	/* Pre-load to get render settings before init_screen */
	sceneT *tmp = load_scene(SCENE_FILE, &RENDER);
	if (!tmp) { fprintf(stderr, "Failed to load %s\n", SCENE_FILE); exit(1); }
	free(tmp);

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
