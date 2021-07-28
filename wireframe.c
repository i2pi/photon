#ifndef NO_GL
#include "gl.h"
#include "tracer.h"
#include "wireframe.h"

#define WIREFRAME

char PRINT_DEBUG = 0;

#define MAX_RAY_DISPLAY_BUFFER 4096
rayT ray_display_buffer[MAX_RAY_DISPLAY_BUFFER];
int	 ray_display_idx = 0;
char ray_display_buffer_full = 0;
rayT seg_display_buffer[MAX_RAY_DISPLAY_BUFFER];
float seg_color[MAX_RAY_DISPLAY_BUFFER][4];
int	 seg_display_idx = 0;
char seg_display_buffer_full = 0;

void gl_show_seg (rayT *seg, float color[4]) {
	glBegin(GL_LINES);
	glColor4fv(color);
	glVertex3f(seg->origin.x, seg->origin.y, seg->origin.z);
	glVertex3f(seg->direction.x, seg->direction.y, seg->direction.z);
	glEnd();
}

void reset_wireframe_buffers (void) {
	seg_display_idx = 0;
	seg_display_buffer_full = 0;
	ray_display_idx = 0;
	ray_display_buffer_full = 0;
}


void add_ray_to_display_buffer (rayT *ray) {
	int i;
	i = ray_display_idx++;
	if (i >= MAX_RAY_DISPLAY_BUFFER) {
		i = 0;
		ray_display_idx = i;
		ray_display_buffer_full = 1;
	}
	ray_display_buffer[i] = *ray;
}

void add_seg_to_display_buffer (vectorT *start, vectorT *end, float r, float g, float b) {
	int i;
	rayT seg;

	seg.origin = *start;
	seg.direction = *end;
	i = seg_display_idx++;
	if (i >= MAX_RAY_DISPLAY_BUFFER) {
		i = 0;
		seg_display_idx = i;
		seg_display_buffer_full = 1;
	}
	seg_display_buffer[i] = seg;
	seg_color[i][0] = r;
	seg_color[i][1] = g;
	seg_color[i][2] = b;
	seg_color[i][3] = 1;
}

void display_ray_buffer(void) {
	int i, n;
	n = ray_display_buffer_full ? MAX_RAY_DISPLAY_BUFFER : ray_display_idx;
	for (i=0; i<n; i++) {
		rayT *r = &ray_display_buffer[i];
		gl_show_ray(r->origin.x, r->origin.y, r->origin.z,
				    r->direction.x, r->direction.y, r->direction.z);
	}
	n = seg_display_buffer_full ? MAX_RAY_DISPLAY_BUFFER : seg_display_idx;
	for (i=0; i<n; i++) {
		gl_show_seg(&seg_display_buffer[i], seg_color[i]);
	}
}

#endif //NO_GL
