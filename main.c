#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <unistd.h> 
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "gl.h"
#include "tracer.h"
#include "wireframe.h"

#define ESCAPE 27

#define SCREEN_TEXTURE_ID	1


#define SCREEN_WIDTH	  10800
#define SCREEN_HEIGHT	  10800

#define THREADS       32
#define MIN_SAMPLES   16
#define MAX_SAMPLES    1000
#define QUAL_THRESH   0.001
#define TRACE_DEPTH   64

#define PI 3.1415926535

sceneT	*SCENE;

char	*SCREEN_PIXELS;
float   *SCREEN_PIXELS_F;

float	clamp (float x) {
	if (x < 1) return (x);
	return (1);
}

float gamma(float x) {
    x = pow(x-0.05, 1.1);
    return (clamp(x));
}

char single_ray_trace_to_pixels (sceneT *scene, int width, int height, int x, int y, char *pixels, float *pixels_f) {
  /*
   * Unused. Casts a single ray without sampling
   */
	rayT	ray, camera_ray;
	rayT 	*ret;

	ray.origin.x = 0.0f;
	ray.origin.y = 0.0f;
	ray.origin.z = scene->camera.z;

	ray.direction.x = 2.0*(x / (float) width) - 1.0;
	ray.direction.y = 2.0*(y / (float) height) - 1.0;
	ray.direction.z = -2.445; // TODO: calc from fov

	normalize_vector(&ray.direction);

	ray.refractive_index = 1.0;

	memset (&ray.color[0], 0, sizeof(float) * 4);

	if (!cast_ray_through_camera (&ray, &SCENE->camera, &camera_ray)) return (0);
	
	ret = cast_ray (&camera_ray, SCENE, TRACE_DEPTH);
	if (ret) {
		pixels[((y*width) + x)*3 + 0] = gamma(ret->color[0]) * 255;
		pixels[((y*width) + x)*3 + 1] = gamma(ret->color[1]) * 255;
		pixels[((y*width) + x)*3 + 2] = gamma(ret->color[2]) * 255;

		pixels_f[((y*width) + x)*3 + 0] = ret->color[0];
		pixels_f[((y*width) + x)*3 + 1] = ret->color[1];
		pixels_f[((y*width) + x)*3 + 2] = ret->color[2];
		return (1);
	}
	return (0);	
}

void	sample_circle (float R, float *x, float *y) {
	float	t, s;

	t = 2.0 * PI * (random() / (float) RAND_MAX);
	s = R * (random() / (float) RAND_MAX);

	*x = s * cos(t);
	*y = s * sin(t);
}

char single_ray_trace_to_sensor (sceneT *scene, int width, int height, int x, int y, int min_samples, int max_samples, float thresh, char *pixels, float *pixels_f) {
	int		i, j;
	rayT	ray, camera_ray;
	rayT 	*ret;
	char	hit;
	float 	X, Y;
  float r, g, b;
	float	R, G, B;
	vectorT	sensor_normal;

  float l_buf[8192];

  if (max_samples > 8192) {
      fprintf (stderr, "max_samples (%d) > 1024", max_samples);
      exit (-1);
  }

	sensor_normal.x = 0;
	sensor_normal.y = 0;
	sensor_normal.z = -1;

	X = 2.0*(0.5 - (x / (float) width));
	Y = 2.0*(0.5 - (y / (float) height));

	ray.origin.x = scene->camera.d * X;
	ray.origin.y = scene->camera.d * Y;
	ray.origin.z = scene->camera.z;

	ray.refractive_index = 1.0;

	R = G = B = 0.0;

	for (i=0; i<max_samples; i++) {
		vectorT p;

    sample_circle(0.05, &p.x, &p.y);
    p.z = scene->camera.lens[0].z;

  	diff_vector(&p, &ray.origin, &ray.direction);
  	normalize_vector(&ray.direction);

  	memset (&ray.color[0], 0, sizeof(float) * 4);

  	hit = cast_ray_through_camera (&ray, &SCENE->camera, &camera_ray);
  	if (!hit) {
     // TODO: This happens :)
  	  printf ("this should never happen\n");
     continue;
  	}

  	ret = cast_ray (&camera_ray, SCENE, TRACE_DEPTH);

  	if (ret) {
			float cosine = 0.5 + 0.5* dot_vector(&sensor_normal, &camera_ray.direction);
			r = ret->color[0] * cosine;
			g = ret->color[1] * cosine;
			b = ret->color[2] * cosine;

      R += r;
      G += g;
      B += b;
  	} else {
      r = g = b = 0.0;
    }

    /*
     * Instead of taking MAX_SAMPLES for each ray, we keep a buffer
     * of the running mean of the luminance (r+g+b). When the last
     * MIN_SAMPLES of the running luminance doesn't vary by more 
     * than QUAL_THRESH %, we cut short the sampling.
     */

    // TODO: Circular buffer
    if (i == 0) {
      l_buf[i] = r + g + b;
    } else {
      l_buf[i] = l_buf[i-1] + ( (r + g + b - l_buf[i-1]) / (float) (i + 1.0) );
    }

    if (i > min_samples) {
      for (j=1; j<min_samples; j++) {
        float score = fabs(l_buf[i-j] - l_buf[i]) / (l_buf[i]);
        if (score > thresh) break;
      }
      if (j == min_samples) break;
    } 
  }
/*
  pixels[((y*width) + x)*3 + 0] = 255 * (i / 500.0);
  pixels[((y*width) + x)*3 + 1] = 255 * (i / 500.0); 
  pixels[((y*width) + x)*3 + 2] = 255 * (i / 500.0);
*/
	pixels[((y*width) + x)*3 + 0] = gamma(R / (float) i) * 255;
	pixels[((y*width) + x)*3 + 1] = gamma(G / (float) i) * 255;
	pixels[((y*width) + x)*3 + 2] = gamma(B / (float) i) * 255;

        pixels_f[((y*width) + x)*3 + 0] = R / (float) i;
        pixels_f[((y*width) + x)*3 + 1] = G / (float) i;
        pixels_f[((y*width) + x)*3 + 2] = B / (float) i;

	return (1);
}

volatile int running_threads = 0;
pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  sceneT  *scene;
  int     width, height;
  int     y_start, y_end;
  char    *pixels;
  float   *pixels_f;
} bundle_argsT;

void *ray_trace_bundle_to_pixels (bundle_argsT *args) {
  /*
   * Ray traces a y-section of the screen. Intended to be run
   * inside a thread. We divide by y to give some semblance of 
   * cache coherency.
   */
  int x, y;

  for (y=args->y_start; y<args->y_end; y++) {
    for (x=0; x<args->width; x++) {
      single_ray_trace_to_sensor (args->scene, args->width, args->height, 
          x, y, MIN_SAMPLES, MAX_SAMPLES, QUAL_THRESH, args->pixels, args->pixels_f);
    }
  }

  pthread_mutex_lock(&running_mutex);
  running_threads--;
  pthread_mutex_unlock(&running_mutex);

  return (NULL);
}

void	ray_trace_to_pixels (sceneT *scene, int width, int height, char *pixels, float *pixels_f) {
  int   t, y, step;
  pthread_t thread[THREADS];
  bundle_argsT  arg[THREADS];


  step = floor(height / (float) THREADS);

  running_threads = THREADS;
  y = 0;

  for (t=0; t<THREADS; t++) {
      arg[t].scene = scene;
      arg[t].width = width;
      arg[t].height = height;
      arg[t].y_start = y;
      arg[t].y_end = y  + step;

      y = arg[t].y_end;
      if (t == THREADS - 1) arg[t].y_end = height;
      arg[t].pixels = pixels;
      arg[t].pixels_f = pixels_f;
      pthread_create(&thread[t], NULL, ray_trace_bundle_to_pixels, &arg[t]);
  }

  while (running_threads > 0) {
    usleep(1000);
  }

}

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
  color_object (obj, pink, 0.0,1.0, 0.0, 1.0);
  add_object_to_scene (s, obj);

  for (i=0; i<6; i++) {
    obj = create_sphere_object(0.41*sin(PI * (idx/32.0) + i*0.20) + xos, 0.0+yos, -2.75+zos, 1.04+pow(i,1.155)*(0.17 + 0.17*sin(i*0.98 + PI * idx/128.0)));
    color_object (obj, sky, 0.0,0.0, 0.97 -  i*0.025, 1.1 + i*0.09*sin(PI * idx / 48.0));

    add_object_to_scene (s, obj);
  }

  obj = create_ortho_plane_object(0, 0, 1, -800);
  color_object (obj, white, 0.2,0.5, 0.0, 1);
  add_object_to_scene (s, obj);

  add_lens_to_camera(&s->camera, 0, 1.7,2.8 + 0.7*sin((32+idx) / 64.0), 0.1, 0.94 + 0.2 * sin(PI * (idx +17) / 192.0));
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
	ray_trace_to_pixels(SCENE, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_PIXELS, SCREEN_PIXELS_F);
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


	init_gl (argc, argv);

	init_screen ();
  
	glutMainLoop();  

	return (1);
}
