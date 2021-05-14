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
<pre>
<a href="../">../</a>
<a href="docs/">docs/</a>                                             2019-08-23 15:27:48        -
<a href="examples/">examples/</a>                                         2019-08-23 15:27:52        -
<a href="README.txt">README.txt</a>                                        2019-08-23 19:15:42        0
<a href="release/">release/</a>                                          2019-08-23 15:28:52        -
</pre>
  <hr>
</body>
</html>
 */
void make_index_of_page(const char* dir, std::string& page, const char* url = "");

#endif // HV_HTTP_PAGE_H_
