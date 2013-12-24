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


void set_camera (void) {
	float	fov = 45.0f;
	float	aspect = 1.0f;
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();			
	gluPerspective(fov,aspect,0.1f,100.0f);
	glMatrixMode(GL_MODELVIEW);
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

	glBegin(GL_TRIANGLES);                

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

			// pointing up
			glNormal3f(norm[j].x, norm[j].y, norm[j].z);
			glVertex3f(circle[j].x, circle[j].y, circle[j].z);	
			glNormal3f(norm[n].x, norm[n].y, norm[n].z);
			glVertex3f(circle[n].x, circle[n].y, circle[n].z);	
			glNormal3f(p_norm[j].x, p_norm[j].y, p_norm[j].z);
			glVertex3f(p_circle[j].x, p_circle[j].y, p_circle[j].z);	

			// pointing down
			glNormal3f(p_norm[j].x, p_norm[j].y, p_norm[j].z);
			glVertex3f(p_circle[j].x, p_circle[j].y, p_circle[j].z);	
			glNormal3f(p_norm[n].x, p_norm[n].y, p_norm[n].z);
			glVertex3f(p_circle[n].x, p_circle[n].y, p_circle[n].z);	
			glNormal3f(norm[n].x, norm[n].y, norm[n].z);
			glVertex3f(circle[n].x, circle[n].y, circle[n].z);	
		}

		memcpy (p_circle, circle, sizeof(vectorT) * segments);
		memcpy (p_norm, norm, sizeof(vectorT) * segments);
	}

	glEnd();
}

void gl_triangle(float *vertex) {
	glBegin(GL_TRIANGLES);
	glNormal3f(vertex[9], vertex[10], vertex[11]);
	glVertex3f(vertex[0], vertex[1], vertex[2]);
	glVertex3f(vertex[3], vertex[4], vertex[5]);
	glVertex3f(vertex[6], vertex[7], vertex[8]);
	glEnd();
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
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA);

    glutInitWindowSize(Width, Height);

    gui_state.window = glutCreateWindow("i2photon");
    glutDisplayFunc(render_scene);
    glutIdleFunc(render_scene);
    glutReshapeFunc(&ReSizeGLScene);
    glutKeyboardFunc(&keyPressed);

	glEnable(GL_TEXTURE_2D);
	glClearColor(1,1,1,1);
	glClearDepth(1);				
	glEnable(GL_LINE_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST); 
	glDepthFunc(GL_LESS);			   
	glEnable(GL_DEPTH_TEST);		  
	glShadeModel(GL_SMOOTH);		
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_LIGHTING);                 	//enables lighting
	glEnable(GL_LIGHT0);                   	//enables a light
	glLightModeli(GL_LIGHT_MODEL_TWO_SIDE,0);  
	glEnable(GL_COLOR_MATERIAL);

	gui_state.w = Width;
	gui_state.h = Height;

	set_camera();
}
