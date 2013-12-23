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


int start_gl (int argc, char **argv, void (*draw_scene)(void));
void print (float x, float y, char *text);


#endif
