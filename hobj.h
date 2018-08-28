#ifndef H_OBJ_H
#define H_OBJ_H

#include "hdef.h"
#include "hvar.h"
#include <string>
#include <map>
#include <list>

class HObj{
public:
    HObj(HObj* parent = NULL) {
        _parent = parent;
    }

    virtual ~HObj() {
        auto iter = children.begin();
        while (iter != children.end()) {
            SAFE_DELETE(*iter);
            iter++;
        }
    }

    std::string name() {
        return _objName;
    }

    void  setName(char* name) {
        _objName = name;
    }

    HObj* parent() {
        return _parent;
    }

    void  setParent(HObj* ptr) {
        _parent = ptr;
    }

    void  setChild(HObj* ptr) {
        children.push_back(ptr);
    }

    HObj* findChild(std::string objName) {
        auto iter = children.begin();
        while (iter != children.end()) {
            if ((*iter)->name() == objName)
                return *iter;
            iter++;
        }
    }

    HVar  property(std::string key) {
        auto iter = properties.find(key);
        if (iter != properties.end())
            return iter->second;
        return HVar();
    }

    void  setProperty(std::string key, HVar value) {
        properties[key] = value;
    }

    method_t method(std::string key) {
        auto iter = methods.find(key);
        if (iter != methods.end())
            return iter->second;
        return NULL;
    }

    void  setMethod(std::string key, method_t method) {
        methods[key] = method;
    }

public:
    std::string _objName;
    std::map<std::string, HVar> properties;
    std::map<std::string, method_t> methods;

    HObj* _parent;
    std::list<HObj*> children;
};

#endif