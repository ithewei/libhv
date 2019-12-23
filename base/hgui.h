#ifndef HV_GUI_H_
#define HV_GUI_H_

typedef unsigned int HColor;  // 0xAARRGGBB

#define CLR_B(c)    (c         & 0xff)
#define CLR_G(c)    ((c >> 8)  & 0xff)
#define CLR_R(c)    ((c >> 16) & 0xff)
#define CLR_A(c)    ((c >> 24) & 0xff)
#define ARGB(a, r, g, b) MAKE_FOURCC(a, r, g, b)

typedef struct hpoint_s {
    int x;
    int y;

#ifdef __cplusplus
    hpoint_s() {
        x = y = 0;
    }

    hpoint_s(int x, int y) {
        this->x = x;
        this->y = y;
    }
#endif
} HPoint;

typedef struct hsize_s {
    int w;
    int h;

#ifdef __cplusplus
    hsize_s() {
        w = h = 0;
    }

    hsize_s(int w, int h) {
        this->w = w;
        this->h = h;
    }
#endif
} HSize;

typedef struct hrect_s {
    int x;
    int y;
    int w;
    int h;

#ifdef __cplusplus
    hrect_s() {
        x = y = w = h = 0;
    }

    hrect_s(int x, int y, int w, int h) {
        this->x = x;
        this->y = y;
        this->w = w;
        this->h = h;
    }

    int left()     {return x;}
    int right()    {return x+w;}
    int top()      {return y;}
    int bottom()   {return y+h;}
#endif
} HRect;

#endif // HV_GUI_H_
