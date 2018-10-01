#ifndef H_STRING_H
#define H_STRING_H

#include <string>
#include <vector>
using std::string;

typedef std::vector<string> StringList;

string asprintf(const char* fmt, ...);
StringList split(const string& str, char delim);
string trim(const string& str);
string trimL(const string& str);
string trimR(const string& str);
string replace(const string& str, const string& find, const string& rep);

string basename(const string& str);
string dirname(const string& str);
string filename(const string& str);
string suffixname(const string& str);

#endif // H_STRING_H
