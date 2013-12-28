#ifndef __WIREFRAME_H__
#define __WIREFRAME_H__

#define WIREFRAME

extern char	PRINT_DEBUG;

void add_ray_to_display_buffer (rayT *ray);
void add_seg_to_display_buffer (vectorT *start, vectorT *end, float r, float g, float b);
void display_ray_buffer(void);
void reset_wireframe_buffers (void) ;

#endif
