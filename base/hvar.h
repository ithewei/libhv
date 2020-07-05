#ifndef HV_VAR_H_
#define HV_VAR_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

class HVar {
public:
    enum Type {
        UNKNOWN,
        BOOLEAN,
        INTEGER,
        FLOAT,
        STRING,
        POINTER
    } type;

    union Data {
        bool        b;
        int64_t     i;
        double      f;
        char*       str;
        void*       ptr;
    } data;

    HVar()          {memset(&data, 0, sizeof(data)); type = UNKNOWN;}
    HVar(bool b)    {data.b = b; type = BOOLEAN;}
    HVar(int64_t i) {data.i = i; type = INTEGER;}
    HVar(double f)  {data.f = f; type = FLOAT;}
    HVar(char* str) {
        data.str = (char*)malloc(strlen(str)+1);
        strcpy(data.str, str);
        type = STRING;
    }
    HVar(void* ptr) {data.ptr = ptr; type = POINTER;}

    ~HVar() {
        if (type == STRING && data.str) {
            free(data.str);
            data.str = NULL;
        }
    }

    bool    isNull()    {return type == UNKNOWN;}
    bool    isValid()   {return type != UNKNOWN;}

    bool    toBool()    {return data.b;}
    int64_t toInt()     {return data.i;}
    double  toFloat()   {return data.f;}
    char*   toString()  {return data.str;}
    void*   toPointer() {return data.ptr;}
};

#endif // HV_VAR_H_
