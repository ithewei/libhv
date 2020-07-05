#ifndef HV_OBJ_H_
#define HV_OBJ_H_

#include <string>
#include <map>
#include <list>

#include "hvar.h"

typedef int (*HMethod)(void* userdata);

class HObj {
public:
    HObj(HObj* parent = NULL) {
        this->parent = parent;
    }

    virtual ~HObj() {
        deleteAllChild();
    }

    void setName(char* name) {
        this->name = name;
    }

    void setParent(HObj* parent) {
        this->parent = parent;
    }

    bool addChild(HObj* child) {
        children.push_back(child);
        return true;
    }

    bool removeChild(HObj* child) {
        auto iter = children.begin();
        while (iter != children.end()) {
            if (*iter == child) {
                iter = children.erase(iter);
                return true;
            }
            ++iter;
        }
        return false;
    }

    void deleteAllChild() {
        auto iter = children.begin();
        while (iter != children.end()) {
            if (*iter) {
                delete (*iter);
            }
            ++iter;
        }
        children.clear();
    }

    HObj* findChild(std::string name) {
        for (auto iter = children.begin(); iter != children.end(); ++iter) {
            if ((*iter)->name == name) {
                return *iter;
            }
        }
        return NULL;
    }

    HVar property(std::string key) {
        auto iter = properties.find(key);
        if (iter != properties.end()) {
            return iter->second;
        }
        return HVar();
    }

    void setProperty(std::string key, HVar value) {
        properties[key] = value;
    }

    HMethod method(std::string key) {
        auto iter = methods.find(key);
        if (iter != methods.end())
            return iter->second;
        return NULL;
    }

    void setMethod(std::string key, HMethod method) {
        methods[key] = method;
    }

public:
    HObj*               parent;
    std::list<HObj*>    children;

    std::string                     name;
    std::map<std::string, HVar>     properties;
    std::map<std::string, HMethod>  methods;
};

#endif // HV_OBJ_H_
