#ifndef HV_OBJ_H_
#define HV_OBJ_H_

#include <string>
#include <map>
#include <list>

#include "hdef.h"
#include "hvar.h"

class HObj {
 public:
    HObj(HObj* parent = NULL) {
        _parent = parent;
    }

    virtual ~HObj() {
        deleteAllChild();
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

    void  addChild(HObj* ptr) {
        _children.push_back(ptr);
    }

    void removeChild(HObj* ptr) {
        auto iter = _children.begin();
        while (iter != _children.end()) {
            if ((*iter) == ptr) {
                iter = _children.erase(iter);
            } else {
                ++iter;
            }
        }
    }

    void deleteAllChild() {
        auto iter = _children.begin();
        while (iter != _children.end()) {
            SAFE_DELETE(*iter);
            ++iter;
        }
        _children.clear();
    }

    HObj* findChild(std::string objName) {
        for (auto iter = _children.begin(); iter != _children.end(); ++iter) {
            if ((*iter)->name() == objName) {
                return *iter;
            }
        }
        return NULL;
    }

    HVar  property(std::string key) {
        auto iter = _properties.find(key);
        if (iter != _properties.end())
            return iter->second;
        return HVar();
    }

    void  setProperty(std::string key, HVar value) {
        _properties[key] = value;
    }

    method_t method(std::string key) {
        auto iter = _methods.find(key);
        if (iter != _methods.end())
            return iter->second;
        return NULL;
    }

    void  setMethod(std::string key, method_t method) {
        _methods[key] = method;
    }

 public:
    std::string _objName;
    std::map<std::string, HVar> _properties;
    std::map<std::string, method_t> _methods;

    HObj* _parent;
    std::list<HObj*> _children;
};

#endif // HV_OBJ_H_
