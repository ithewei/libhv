#include "hstring.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <iostream>
#include <sstream>

int vscprintf(const char* fmt, va_list ap) {
	return vsnprintf(NULL, 0, fmt, ap);
}

std::string asprintf(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int len = vscprintf(fmt, ap) + 1;
	va_end(ap);
	// must recall va_start in linux
	va_start(ap, fmt);
	char* buf = (char*)malloc(len);
	vsnprintf(buf, len, fmt, ap);
	va_end(ap);
	buf[len-1] = '\0';
	std::string res(buf);
	free(buf);
	return res;
}

StringList split(std::string& str, char delim){
	std::stringstream ss;
	ss << str;
	std::string item;
	StringList res;
	while (std::getline(ss, item, delim)){
		res.push_back(item);
	}
	return res;
}

std::string trim(std::string& str){
	std::string::size_type pos1 = str.find_first_not_of(" \t\r\n");
	if (pos1 == std::string::npos)	return "";

	std::string::size_type pos2 = str.find_last_not_of(" \t\r\n");
	return str.substr(pos1, pos2-pos1+1);
}

std::string replace(std::string& str, std::string& find, std::string& rep){
	std::string::size_type pos = 0;
	std::string::size_type a = find.size();
	std::string::size_type b = rep.size();
	while ((pos = str.find(find,pos)) != std::string::npos){
	    str.replace(pos, a, rep);
		pos += b;
	}
    return str;
}

string basename(string& str){
    string::size_type pos1 = str.find_last_not_of("/\\");
    if (pos1 == string::npos){
        return "/";
    }
    string::size_type pos2 = str.find_last_of("/\\", pos1);
    if (pos2 == string::npos){
        pos2 = 0;
    }else{
        pos2++;
    }

    return str.substr(pos2, pos1-pos2+1);
}

string dirname(string& str){
    string::size_type pos1 = str.find_last_not_of("/\\");
    if (pos1 == string::npos){
        return "/";
    }
    string::size_type pos2 = str.find_last_of("/\\", pos1);
    if (pos2 == string::npos){
        return ".";
    }else if (pos2 == 0){
        pos2 = 1;
    }

    return str.substr(0, pos2);
}

string filename(string& str){
    string::size_type pos1 = str.find_last_of("/\\");
    if (pos1 == string::npos){
        pos1 = 0;
    }else{
        pos1++;
    }
    string file = str.substr(pos1, -1);

    string::size_type pos2 = file.find_last_of(".");
    if (pos2 == string::npos){
        return file;
    }
    return file.substr(0, pos2);
}

string suffixname(string& str){
    string::size_type pos1 = str.find_last_of("/\\");
    if (pos1 == string::npos){
        pos1 = 0;
    }else{
        pos1++;
    }
    string file = str.substr(pos1, -1);

    string::size_type pos2 = file.find_last_of(".");
    if (pos2 == string::npos){
        return "";
    }
    return file.substr(pos2+1, -1);
}
