#ifndef HW_INI_PARSER_H
#define HW_INI_PARSER_H

#include <list>

#include "hdef.h"
#include "hstring.h"

#define DEFAULT_INI_COMMENT "#"
#define DEFAULT_INI_DELIM   "="

/**********************************
# div

[section]

key = value # span

# div
***********************************/

// class IniComment;
// class IniSection;
// class IniKeyValue;
// for simplicity, we add a member value.
class IniNode {
public:
    enum Type {
        INI_NODE_TYPE_UNKNOWN,
        INI_NODE_TYPE_ROOT,
        INI_NODE_TYPE_SECTION,
        INI_NODE_TYPE_KEY_VALUE,
        INI_NODE_TYPE_DIV,
        INI_NODE_TYPE_SPAN,
    } type;
    string  label;  
    string  value;  
    std::list<IniNode*>    children;

    virtual ~IniNode() {
        for (auto& item : children) {
            SAFE_DELETE(item);
        }
        children.clear();
    }

    void Add(IniNode* pNode) {
        children.push_back(pNode);
    }

    void Del(IniNode* pNode) {
        for (auto iter = children.begin(); iter != children.end(); ++iter) {
            if ((*iter) == pNode) {
                delete (*iter);
                children.erase(iter);
                return;
            }
        }
    }

    IniNode* Get(const string& label, Type type = INI_NODE_TYPE_KEY_VALUE) {
        for (auto& pNode : children) {
            if (pNode->type == type && pNode->label == label) {
                return pNode;
            }
        }
        return NULL;
    }
};

// class IniComment : public IniNode {
// public:
//     string comment;
// };

// class IniSection : public IniNode {
// public:
//     string section;
// };

// class IniKeyValue : public IniNode {
// public:
//     string key;
//     string value;
// };

class IniParser {
public:
    IniParser();
    ~IniParser();

    void SetIniComment(const string& comment) {_comment = comment;}
    void SetIniDilim(const string& delim) {_delim = delim;}

    int LoadFromFile(const char* filepath);
    int LoadFromMem(const char* data);
    int Unload();

    void DumpString(IniNode* pNode, string& str);
    string DumpString();
    int Save();
    int SaveAs(const char* filepath);

    string GetValue(const string& key, const string& section = "");
    void   SetValue(const string& key, const string& value, const string& section = "");

private:
    string  _comment;
    string  _delim;
    string  _filepath;

    IniNode* root_;
};

#endif // HW_INI_PARSER_H