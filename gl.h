#ifndef __GL_H__
#define __GL_H__

#ifdef MAC
	#include <GLUT/glut.h>
	#include <OpenGL/gl.h>
#else
	#include <GL/glut.h>
	#include <GL/gl.h>
	#include <GL/glu.h>
#endif

#include "tracer.h"

typedef struct gui_stateT
{
    int window;

    int w, h;

    struct {
        double x, y;
        int button, state;
    } mouse;
} gui_stateT;

gui_stateT  gui_state;


void init_gl (int argc, char **argv);
void gl_sphere (float x, float y, float z, float r, int segments);
void gl_cube(float x, float y, float z, float d);
void set_camera();

extern void render_scene (void);

void init_texture_for_pixels (int tex_id);
void draw_pixels_to_texture (char *pixels, int w, int h, int tex_id);


#endif
