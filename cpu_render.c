#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "tracer.h"
#include "cpu_render.h"

#define PI 3.1415926535

static float clamp(float x) {
	if (x < 1) return (x);
	return (1);
}

static float gamma(float x) {
    x = pow(x-0.05, 1.1);
    return (clamp(x));
}

static char single_ray_trace_to_pixels (sceneT *scene, int width, int height, int x, int y, char *pixels, float *pixels_f) {
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

	if (!cast_ray_through_camera (&ray, &scene->camera, &camera_ray)) return (0);

	ret = cast_ray (&camera_ray, scene, 8);
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

static void sample_circle (float R, float *x, float *y) {
	float	t, s;

	t = 2.0 * PI * (random() / (float) RAND_MAX);
	s = R * (random() / (float) RAND_MAX);

	*x = s * cos(t);
	*y = s * sin(t);
}

static char single_ray_trace_to_sensor (sceneT *scene, int width, int height, int x, int y, int min_samples, int max_samples, float thresh, char *pixels, float *pixels_f) {
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

	ray.origin.x = scene->camera.d * X * (width / (float) height);
	ray.origin.y = scene->camera.d * Y;
	ray.origin.z = scene->camera.z;

	ray.refractive_index = 1.0;

	R = G = B = 0.0;

	for (i=0; i<max_samples; i++) {
		vectorT p;

    // Sample a random wavelength for this ray
    float wavelength = 380.0 + 400.0 * (random() / (float) RAND_MAX);
    ray.wavelength = wavelength;

    sample_circle(scene->camera.lens[0].radius, &p.x, &p.y);
    p.z = scene->camera.lens[0].z;

  	diff_vector(&p, &ray.origin, &ray.direction);
  	normalize_vector(&ray.direction);

  	memset (&ray.color[0], 0, sizeof(float) * 4);

  	hit = cast_ray_through_camera (&ray, &scene->camera, &camera_ray);
  	if (!hit) {
     continue;
  	}

    camera_ray.wavelength = wavelength;
  	ret = cast_ray (&camera_ray, scene, max_samples);

  	if (ret) {
			float cosine = 0.5 + 0.5* dot_vector(&sensor_normal, &camera_ray.direction);
			float wr, wg, wb;
			wavelength_to_rgb(wavelength, &wr, &wg, &wb);
			r = ret->color[0] * wr * cosine;
			g = ret->color[1] * wg * cosine;
			b = ret->color[2] * wb * cosine;

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

	pixels[((y*width) + x)*3 + 0] = gamma(R / (float) i) * 255;
	pixels[((y*width) + x)*3 + 1] = gamma(G / (float) i) * 255;
	pixels[((y*width) + x)*3 + 2] = gamma(B / (float) i) * 255;

        pixels_f[((y*width) + x)*3 + 0] = R / (float) i;
        pixels_f[((y*width) + x)*3 + 1] = G / (float) i;
        pixels_f[((y*width) + x)*3 + 2] = B / (float) i;

	return (1);
}

static volatile int running_threads = 0;
static pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  sceneT  *scene;
  int     width, height;
  int     y_start, y_end;
  int     min_samples, max_samples;
  float   qual_thresh;
  int     trace_depth;
  char    *pixels;
  float   *pixels_f;
} bundle_argsT;

static void *ray_trace_bundle_to_pixels (void *arg) {
  bundle_argsT *args = (bundle_argsT *) arg;
  /*
   * Ray traces a y-section of the screen. Intended to be run
   * inside a thread. We divide by y to give some semblance of
   * cache coherency.
   */
  int x, y;

  for (y=args->y_start; y<args->y_end; y++) {
    for (x=0; x<args->width; x++) {
      single_ray_trace_to_sensor (args->scene, args->width, args->height,
          x, y, args->min_samples, args->max_samples, args->qual_thresh,
          args->pixels, args->pixels_f);
    }
  }

  pthread_mutex_lock(&running_mutex);
  running_threads--;
  pthread_mutex_unlock(&running_mutex);

  return (NULL);
}

#define THREADS 32

void ray_trace_to_pixels (sceneT *scene, int width, int height,
                          int min_samples, int max_samples,
                          float qual_thresh, int trace_depth,
                          char *pixels, float *pixels_f) {
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
      arg[t].min_samples = min_samples;
      arg[t].max_samples = max_samples;
      arg[t].qual_thresh = qual_thresh;
      arg[t].trace_depth = trace_depth;

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
