#ifndef HGL_H
#define HGL_H

#include <GL/glew.h>
#include "hframe.h"

// GL PixelFormat extend
#define GL_I420				0x1910

typedef struct GLTexture_s{
    GLuint id; // glGenTextures分配的ID
    HFrame frame;
}GLTexture;

#endif // HGL_H
