#ifndef HGL_H
#define HGL_H

#include <GL/glew.h>
#include "hframe.h"

// GL PixelFormat extend
#define GL_I420				0x1910  // YYYYYYYYUUVV
#define GL_YV12             0x1911  // YYYYYYYYVVUU
#define GL_NV12             0x1912  // YYYYYYYYUVUV
#define GL_NV21             0x1913  // YYYYYYYYVUVU

//#define GL_RGB  0x1907            // RGBRGB
//#define GL_RGBA 0x1908            // RGBARGBA

//#define GL_BGR 0x80E0             // BGRBGR       .bmp
//#define GL_BGRA 0x80E1            // BGRABGRA

typedef struct GLTexture_s{
    GLuint id; // for glGenTextures
    HFrame frame;
}GLTexture;

#endif // HGL_H
