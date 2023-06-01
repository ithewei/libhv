一些基础函数

```c

/* hv内存分配/释放函数 */
void* hv_malloc(size_t size);
void* hv_realloc(void* oldptr, size_t newsize, size_t oldsize);
void* hv_calloc(size_t nmemb, size_t size);
void* hv_zalloc(size_t size);
void  hv_free(void* ptr);

// 使用hv分配内存次数
long hv_alloc_cnt();

// 使用hv释放内存次数
long hv_free_cnt();

/* 字符串操作 */
// 字符串转大写
char* hv_strupper(char* str);
// 字符串转小写
char* hv_strlower(char* str);
// 字符串翻转
char* hv_strreverse(char* str);

// 判断字符串是否以xxx开头
bool hv_strstartswith(const char* str, const char* start);

// 判断字符串是否以xxx结尾
bool hv_strendswith(const char* str, const char* end);

// 判断字符串是否包含xxx
bool hv_strcontains(const char* str, const char* sub);

// 安全的strncpy
char* hv_strncpy(char* dest, const char* src, size_t n);

// 安全的strncat
char* hv_strncat(char* dest, const char* src, size_t n);

// 字符查找
char* hv_strnchr(const char* s, char c, size_t n);

// 查找最后一个点（通常用于提取文件后缀）
#define hv_strrchr_dot(str) strrchr(str, '.')

// 查找最后的路径（通常用于分离目录和文件）
char* hv_strrchr_dir(const char* filepath);

// 获取文件名（利用了上面的strrchr_dir）
const char* hv_basename(const char* filepath);

// 获取文件后缀（利用了上面的strrchr_dot）
const char* hv_suffixname(const char* filename);

/* 文件&目录 */
// mkdir -p: 创建目录
int hv_mkdir_p(const char* dir);
// rmdir -p: 删除目录
int hv_rmdir_p(const char* dir);

// 判断路径是否存在
bool hv_exists(const char* path);

// 判断是否是目录
bool hv_isdir(const char* path);

// 判断是否是文件
bool hv_isfile(const char* path);

// 判断是否是链接
bool hv_islink(const char* path);

// 获取文件大小
size_t hv_filesize(const char* filepath);

// 获取可执行文件绝对路径，例如/usr/local/bin/httpd
char* get_executable_path(char* buf, int size);

// 获取可执行文件所在目录，例如/usr/local/bin
char* get_executable_dir(char* buf, int size);

// 获取可执行文件名，例如httpd
char* get_executable_file(char* buf, int size);

// 获取运行目录，例如/home/www/html
char* get_run_dir(char* buf, int size);

// 返回一个随机数
int   hv_rand(int min, int max);

// 返回一个随机字符串
char* hv_random_string(char *buf, int len);

// 1 y on yes true enable返回true（通常用于配置文件）
bool   hv_getboolean(const char* str);

// 解析size字符串
// 1T2G3M4K5B => ?B
size_t hv_parse_size(const char* str);

// 解析时间字符串
// 1w2d3h4m5s => ?s
time_t hv_parse_time(const char* str);

// 解析url字符串
int hv_parse_url(hurl_t* stURL, const char* strURL);

```

单元测试代码见 [unittest/hbase_test.c](../../unittest/hbase_test.c)
