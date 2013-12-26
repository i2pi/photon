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

#define PI 3.14159265

sceneT	*SCENE;
float	tick_length = 0.05;

void set_camera (void) {
	float	fov = 45.0f;
	float	aspect = 1.0f;
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();			
	gluPerspective(fov,aspect,0.1f,100.0f);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();			
}

void gl_positional_light(GLenum light, float x, float y, float z, float *color) {
	
	float pos[4];
#ifndef WIREFRAME
	float nil[4] = {0,0,0,0};
#endif
	pos[0] = x;
	pos[1] = y;
	pos[2] = z;
	pos[3] = 1.0; // Indicates this is a positional light

#ifndef WIREFRAME
	glEnable(light);                   	
	glLightfv (light, GL_POSITION, pos);
	glLightfv (light, GL_DIFFUSE, color);
	glLightfv (light, GL_SPECULAR, color);
	glLightfv (light, GL_AMBIENT, nil);
#endif
}

void ReSizeGLScene(int Width, int Height)
{
	if (Height==0)	Height=1;

	glViewport(0, 0, Width/2, Height);		
	set_camera();
	gui_state.w = Width;
	gui_state.h = Height;
}

void	print (float x, float y, char *text)
{
	char	*p;
	glRasterPos2d(x,y);
	p = text;
	while (*p) {
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *p);
		p++;
	}
}

void keyPressed(unsigned char key, int x, int y) 
{
    usleep(100);

    if (key == ESCAPE) 
    { 
      glutDestroyWindow(gui_state.window); 
      exit(0);                   
    }

	gui_state.last_key = key;
}

unsigned char get_last_key (void) {
	unsigned char c = gui_state.last_key;

	gui_state.last_key = '\0';

	return (c);
}

void gl_sphere (float x, float y, float z, float r, int segments) {
	vectorT *p_circle;
	vectorT *p_norm;
	vectorT *circle;
	vectorT *norm;
	int		i, j;

	p_circle = (vectorT *) malloc (sizeof(vectorT) * segments);
	circle = (vectorT *) malloc (sizeof(vectorT) * segments);
	p_norm = (vectorT *) malloc (sizeof(vectorT) * segments);
	norm = (vectorT *) malloc (sizeof(vectorT) * segments);

	for (i=0; i<segments; i++) {
		p_circle[i].x = x;
		p_circle[i].y = y - r;
		p_circle[i].z = z;
		p_norm[i].x = 0;
		p_norm[i].y = -1;
		p_norm[i].z = 0;
	}	



	for (i=1; i<segments; i++) {
		float h = r * ((2.0f * i / (float) (segments-1)) - 1.0f);
		float R = sqrt(r*r - h*h);
		for (j=0; j<segments; j++) {
			float t = (2.0f * PI * j / (float) segments);
			circle[j].x = x + R*sin(t);
			circle[j].y = y + h;
			circle[j].z = z + R*cos(t);

			norm[j].x = sin(t);
			norm[j].y = h / r;
			norm[j].z = cos(t);
			normalize_vector(&norm[j]); 
		}

		for (j=0; j<segments; j++) {
			int	n = (j+1) % segments;	

			float	vertex[9];	
			float	normal[9];

			// pointing up
			vector_to_array(&norm[j], &normal[0]);
			vector_to_array(&circle[j], &vertex[0]);

			vector_to_array(&norm[n], &normal[3]);
			vector_to_array(&circle[n], &vertex[3]);

			vector_to_array(&p_norm[j], &normal[6]);
			vector_to_array(&p_circle[j], &vertex[6]);
			gl_triangle (vertex, normal);

			// pointing down
			vector_to_array(&p_norm[j], &normal[0]);
			vector_to_array(&p_circle[j], &vertex[0]);

			vector_to_array(&p_norm[n], &normal[3]);
			vector_to_array(&p_circle[n], &vertex[3]);

			vector_to_array(&norm[n], &normal[6]);
			vector_to_array(&circle[n], &vertex[6]);
			gl_triangle (vertex, normal);
		}

		memcpy (p_circle, circle, sizeof(vectorT) * segments);
		memcpy (p_norm, norm, sizeof(vectorT) * segments);
	}
}


void    gl_axes_wireframe (float x, float y, float z) {
    glBegin(GL_LINES);
	glColor4f(1,0,0,1);
    glVertex3f(x, y, z);
    glVertex3f(x+tick_length, y, z);
    glEnd();

    glBegin(GL_LINES);
	glColor4f(0,1,0,1);
    glVertex3f(x, y, z);
    glVertex3f(x, y+tick_length, z);
    glEnd();

    glBegin(GL_LINES);
	glColor4f(0,0,1,1);
    glVertex3f(x, y, z);
    glVertex3f(x, y, z+tick_length);
    glEnd();
}

void gl_show_ray_tick (float vertex[3], float normal[3]) {
	float s = tick_length;

	glBegin(GL_LINES);
	glColor4f(1,1,0,1);
	glVertex3f(vertex[0], vertex[1], vertex[2]);
	glVertex3f(vertex[0]+s*normal[0], 
		vertex[1]+s*normal[1], 
		vertex[2]+s*normal[2]);
	glEnd();
}

void gl_show_ray (float ox, float oy, float oz, float dx, float dy, float dz) {
	float s = 99;

	glBegin(GL_LINES);
	glColor4f(1,1,1,1);
	glVertex3f(ox, oy, oz);
	glVertex3f(ox+s*dx, oy+s*dy, oz+s*dz);
	glEnd();
}



void gl_triangle(float *vertex, float *normal) {
#ifndef WIREFRAME
	glBegin(GL_TRIANGLES);
#else
	glBegin(GL_LINES);
#endif
	glColor4f(0,1,0,1);
	glVertex3f(vertex[0], vertex[1], vertex[2]);
	glNormal3f(vertex[0], vertex[1], vertex[2]);
	glVertex3f(vertex[3], vertex[4], vertex[5]);
	glNormal3f(vertex[3], vertex[4], vertex[5]);
	glVertex3f(vertex[6], vertex[7], vertex[8]);
	glNormal3f(vertex[6], vertex[7], vertex[8]);
#ifdef WIREFRAME
	glVertex3f(vertex[0], vertex[1], vertex[2]);
	glNormal3f(vertex[0], vertex[1], vertex[2]);
#endif
	glEnd();

#ifdef WIREFRAME
	gl_show_ray_tick(&vertex[0], &normal[0]);
#endif
}

void gl_cube(float x, float y, float z, float d)
{
  glBegin(GL_QUADS);                // start drawing the cube.

  // top of cube
  glVertex3f(x+ d,y+ d,z+-d);       // Top Right Of The Quad (Top)
  glVertex3f(x+-d,y+ d,z+-d);       // Top Left Of The Quad (Top)
  glVertex3f(x+-d,y+ d,z+ d);       // Bottom Left Of The Quad (Top)
  glVertex3f(x+ d,y+ d,z+ d);       // Bottom Right Of The Quad (Top)

  // bottom of cube
  glVertex3f(x+ d,y+-d,z+ d);       // Top Right Of The Quad (Bottom)
  glVertex3f(x+-d,y+-d,z+ d);       // Top Left Of The Quad (Bottom)
  glVertex3f(x+-d,y+-d,z+-d);       // Bottom Left Of The Quad (Bottom)
  glVertex3f(x+ d,y+-d,z+-d);       // Bottom Right Of The Quad (Bottom)

  // front of cube
  glVertex3f(x+ d,y+ d,z+ d);       // Top Right Of The Quad (Front)
  glVertex3f(x+-d,y+ d,z+ d);       // Top Left Of The Quad (Front)
  glVertex3f(x+-d,y+-d,z+ d);       // Bottom Left Of The Quad (Front)
  glVertex3f(x+ d,y+-d,z+ d);       // Bottom Right Of The Quad (Front)

  // back of cube.
  glVertex3f(x+ d,y+-d,z+-d);       // Top Right Of The Quad (Back)
  glVertex3f(x+-d,y+-d,z+-d);       // Top Left Of The Quad (Back)
  glVertex3f(x+-d,y+ d,z+-d);       // Bottom Left Of The Quad (Back)
  glVertex3f(x+ d,y+ d,z+-d);       // Bottom Right Of The Quad (Back)

  // left of cube
  glVertex3f(x+-d,y+ d,z+ d);       // Top Right Of The Quad (Left)
  glVertex3f(x+-d,y+ d,z+-d);       // Top Left Of The Quad (Left)
  glVertex3f(x+-d,y+-d,z+-d);       // Bottom Left Of The Quad (Left)
  glVertex3f(x+-d,y+-d,z+ d);       // Bottom Right Of The Quad (Left)

  // Right of cube
  glVertex3f(x+ d,y+ d,z+-d);           // Top Right Of The Quad (Right)
  glVertex3f(x+ d,y+ d,z+ d);       // Top Left Of The Quad (Right)
  glVertex3f(x+ d,y+-d,z+ d);       // Bottom Left Of The Quad (Right)
  glVertex3f(x+ d,y+-d,z+-d);       // Bottom Right Of The Quad (Right)
  glEnd();                  // Done Drawing The Cube
}


void init_texture_for_pixels (int tex_id) {
	glBindTexture(GL_TEXTURE_2D, tex_id);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
}

void draw_pixels_to_texture (char *pixels, int w, int h, int tex_id) {
	glBindTexture(GL_TEXTURE_2D, tex_id);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
}

void init_gl(int argc, char **argv)
{
	int	Width = 512;
	int Height = 256;

	glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH);

    glutInitWindowSize(Width, Height);

    gui_state.window = glutCreateWindow("i2photon");
    glutDisplayFunc(render_scene);
    glutIdleFunc(render_scene);
    glutReshapeFunc(&ReSizeGLScene);
    glutKeyboardFunc(&keyPressed);

	glEnable(GL_TEXTURE_2D);
	glClearColor(1,1,1,0);
	glClearDepth(0);				
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST); 
	glDepthFunc(GL_LESS);			   
	glEnable(GL_DEPTH_TEST);		  
	glShadeModel(GL_SMOOTH);		
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#ifndef WIREFRAME
	glEnable(GL_LIGHTING);                 	//enables lighting
#endif
	glEnable(GL_COLOR_MATERIAL);

	gui_state.w = Width;
	gui_state.h = Height;

	set_camera();
}
