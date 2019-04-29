#ifndef HW_VAR_H_
#define HW_VAR_H_

#include <stdlib.h>
#include <string.h>

#include "hdef.h"

class HVar {
 public:
    enum TYPE {
        UNKNOWN,
        BOOLEAN,
        INTEGER,
        FLOAT,
        STRING,
        POINTER
    } type;

    union DATA {
        bool b;
        int64_t i;
        float64 f;
        char* str;
        void* ptr;
    } data;

    HVar()          {memset(&data, 0, sizeof(data)); type = UNKNOWN;}
    HVar(bool b)    {data.b = b; type = BOOLEAN;}
    HVar(int64_t i)   {data.i = i; type = INTEGER;}
    HVar(float64 f) {data.f = f; type = FLOAT;}
    HVar(char* str) {
        data.str = (char*)malloc(strlen(str)+1);
        strcpy(data.str, str);
        type = STRING;
    }
    HVar(void* ptr) {data.ptr = ptr; type = POINTER;}

    ~HVar() {
        if (type == STRING) {
            SAFE_FREE(data.str);
        }
    }

    bool    isNull()    {return type == UNKNOWN;}
    bool    isValid()   {return type != UNKNOWN;}

    bool    toBool()    {return data.b;}
    int64_t toInt()     {return data.i;}
    float64 toFloat()   {return data.f;}
    char*   toString()  {return data.str;}
    void*   toPointer() {return data.ptr;}
};

#endif  // HW_VAR_H_
