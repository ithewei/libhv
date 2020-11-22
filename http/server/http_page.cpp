#include "http_page.h"
#include "hdir.h"

#define AUTOINDEX_FILENAME_MAXLEN       50

void make_http_status_page(http_status status_code, std::string& page) {
    char szCode[8];
    snprintf(szCode, sizeof(szCode), "%d ", status_code);
    const char* status_message = http_status_str(status_code);
    page += R"(<!DOCTYPE html>
<html>
<head>
  <title>)";
    page += szCode; page += status_message;
    page += R"(</title>
</head>
<body>
  <center><h1>)";
    page += szCode; page += status_message;
    page += R"(</h1></center>
  <hr>
</body>
</html>)";
}

void make_index_of_page(const char* dir, std::string& page, const char* url) {
    std::list<hdir_t> dirs;
    listdir(dir, dirs);
    char c_str[1024] = {0};
    snprintf(c_str, sizeof(c_str), R"(<!DOCTYPE html>
<html>
<head>
  <title>Index of %s</title>
</head>
<body>
  <h1>Index of %s</h1>
  <hr>
<pre>
)", url, url);
    page += c_str;
    for (auto& item : dirs) {
        if (item.name[0] == '.' && item.name[1] == '\0') continue;
        int len = strlen(item.name) + (item.type == 'd');
        // name
        snprintf(c_str, sizeof(c_str), "<a href=\"%s%s\">%s%s</a>",
                item.name,
                item.type == 'd' ? "/" : "",
                len < AUTOINDEX_FILENAME_MAXLEN ? item.name : std::string(item.name, item.name+AUTOINDEX_FILENAME_MAXLEN-4).append("...").c_str(),
                item.type == 'd' ? "/" : "");
        page += c_str;
        if (strcmp(item.name, "..") != 0) {
            // mtime
            struct tm* tm = localtime(&item.mtime);
            snprintf(c_str, sizeof(c_str), "%04d-%02d-%02d %02d:%02d:%02d        ",
                    tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
            page += std::string(AUTOINDEX_FILENAME_MAXLEN - len, ' ');
            page += c_str;
            // size
            if (item.type == 'd') {
                page += '-';
            }
            else {
                float hsize;
                if (item.size < 1024) {
                    snprintf(c_str, sizeof(c_str), "%lu", (unsigned long)item.size);
                }
                else if ((hsize = item.size/1024.0f) < 1024.0f) {
                    snprintf(c_str, sizeof(c_str), "%.1fK", hsize);
                }
                else if ((hsize /= 1024.0f) < 1024.0f) {
                    snprintf(c_str, sizeof(c_str), "%.1fM", hsize);
                }
                else {
                    hsize /= 1024.0f;
                    snprintf(c_str, sizeof(c_str), "%.1fG", hsize);
                }
                page += c_str;
            }
        }
        page += "\r\n";
    }
    page += R"(</pre>
  <hr>
</body>
</html>)";
}
