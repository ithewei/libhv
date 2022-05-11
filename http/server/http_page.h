#ifndef HV_HTTP_PAGE_H_
#define HV_HTTP_PAGE_H_

#include <string>

#include "httpdef.h"

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
  <title>Index of /downloads/</title>
</head>
<body>
  <h1>Index of /downloads/</h1>
  <hr>
  <table border="0">
    <tr>
      <th align="left" width="30%">Name</th>
      <th align="left" width="20%">Date</th>
      <th align="left" width="20%">Size</th>
    </tr>
    <tr>
      <td><a href="../">../</a></td>
    </tr>
    <tr>
      <td><a href="libhv-vs-nginx.png">libhv-vs-nginx.png</a></td>
      <td>2021-03-10 12:33:57</td>
      <td>211.4K</td>
    </tr>
      <td><a href="中文.html">中文.html</a></td>
      <td>2022-04-25 15:37:12</td>
      <td>191</td>
    </tr>
  </table>
  <hr>
</body>
</html>
 */
void make_index_of_page(const char* dir, std::string& page, const char* url = "");

#endif // HV_HTTP_PAGE_H_
