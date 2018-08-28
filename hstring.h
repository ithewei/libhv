#ifndef H_STRING_H
#define H_STRING_H

#include <string>
#include <vector>
using std::string;

typedef std::vector<string> StringList;

string asprintf(const char* fmt, ...);
StringList split(string& str, char delim);
string trim(string& str);

string basename(string& str);
string dirname(string& str);
string filename(string& str);
string suffixname(string& str);

#endif // H_STRING_H
