#ifndef HTTP_PAGE_H_
#define HTTP_PAGE_H_

#include <string>

#include "http_parser.h"

/*
<!DOCTYPE html>
<html>
<head>
  <title>404 Not Found</title>
</head>
<body>
  <center><h1>404 Not Found</h1></center>
  <hr>
</body>
</html>
 */
void make_http_status_page(http_status status_code, std::string& page);

/*
<!DOCTYPE html>
<html>
<head>
  <title>Index of</title>
</head>
<body>
  <h1>Index of</h1>
  <hr>
  <pre>
  <a href="../">../</a>
  <a href="images/">images/</a>                                  2019-08-22 19:06:05                 -
  <a href="README.txt">README.txt</a>                                2019-08-22 19:06:05                 8.88K
  </pre>
  <hr>
</body>
</html>
 */
void make_index_of_page(const char* dir, std::string& page, const char* url = "");

#endif // HTTP_PAGE_H_
