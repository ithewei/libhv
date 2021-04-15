#ifndef HV_INI_PARSER_H_
#define HV_INI_PARSER_H_

#include <string>
using std::string;

#include "hexport.h"

#define DEFAULT_INI_COMMENT "#"
#define DEFAULT_INI_DELIM   "="

// fwd
class IniNode;

class HV_EXPORT IniParser {
public:
    IniParser();
    ~IniParser();

    int LoadFromFile(const char* filepath);
    int LoadFromMem(const char* data);
    int Unload();
    int Reload();

    string DumpString();
    int Save();
    int SaveAs(const char* filepath);

    string GetValue(const string& key, const string& section = "");
    void   SetValue(const string& key, const string& value, const string& section = "");

    // T = [bool, int, float]
    template<typename T>
    T Get(const string& key, const string& section = "", T defvalue = 0);

    // T = [bool, int, float]
    template<typename T>
    void Set(const string& key, const T& value, const string& section = "");

protected:
    void DumpString(IniNode* pNode, string& str);

public:
    string  _comment;
    string  _delim;
    string  _filepath;
private:
    IniNode* root_;
};

#endif // HV_INI_PARSER_H_
