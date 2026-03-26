# FileCacheEx — 增强版文件缓存

`FileCacheEx` 是 `FileCache` 的增强替代方案，解决了原始实现中的线程安全问题，并提供运行时可配置的参数。

## 头文件

```c++
#include "FileCacheEx.h"
```

## 与 FileCache 的区别

| 特性 | FileCache | FileCacheEx |
|------|-----------|-------------|
| HTTP 头部预留空间 | 编译期固定 1024 字节 | 运行时可配置，默认 4096 字节 |
| `prepend_header()` | 返回 `void`，溢出静默丢弃 | 返回 `bool`，失败返回 `false` |
| 缓存条目线程安全 | 无锁保护 | 每条目 `std::mutex` |
| `resize_buf()` 后 httpbuf | 可能悬空引用 | 主动置空，防止 UAF |
| 文件读取 | 单次 `read()`，可能短读 | 循环读取，处理 `EINTR` |
| `put()` 时机 | 初始化前放入缓存 | 完全初始化后放入缓存 |
| 最大文件大小 | 编译期宏 `FILE_CACHE_MAX_SIZE` | 运行时 `SetMaxFileSize()` |
| DLL 导出 | 无 | `HV_EXPORT` |

## 数据结构

```c++
// 缓存条目
typedef struct file_cache_ex_s {
    mutable std::mutex  mutex;          // 条目级互斥锁
    std::string         filepath;
    struct stat         st;
    time_t              open_time;
    time_t              stat_time;
    uint32_t            stat_cnt;
    HBuf                buf;            // header_reserve + file_content
    hbuf_t              filebuf;        // 指向 buf 中文件内容区域
    hbuf_t              httpbuf;        // 指向 buf 中 header + 文件内容区域
    char                last_modified[64];
    char                etag[64];
    std::string         content_type;
    int                 header_reserve; // 头部预留字节数
    int                 header_used;    // 实际使用的头部字节数

    // 方法
    bool is_modified();                         // 检查文件是否被修改（需持锁）
    bool is_complete();                         // 检查缓存是否完整（需持锁）
    void resize_buf(size_t filesize, int reserved);  // 重新分配缓冲区（需持锁）
    void resize_buf(size_t filesize);                // 使用当前 header_reserve
    bool prepend_header(const char* header, int len); // 线程安全：写入 HTTP 头

    // 线程安全访问器
    int  get_header_reserve()   const;
    int  get_header_used()      const;
    int  get_header_remaining() const;
    bool header_fits(int len)   const;
} file_cache_ex_t;

typedef std::shared_ptr<file_cache_ex_t> file_cache_ex_ptr;
```

## FileCacheEx 类

```c++
class HV_EXPORT FileCacheEx : public hv::LRUCache<std::string, file_cache_ex_ptr> {
public:
    // 可配置参数
    int stat_interval;      // stat() 检查间隔（秒），默认 10
    int expired_time;       // 缓存过期时间（秒），默认 60
    int max_header_length;  // 每条目头部预留字节，默认 4096
    int max_file_size;      // 最大缓存文件大小，默认 4MB

    explicit FileCacheEx(size_t capacity = 100);

    // 打开文件并缓存
    file_cache_ex_ptr Open(const char* filepath, OpenParam* param);

    // 检查文件是否在缓存中
    bool Exists(const char* filepath) const;

    // 从缓存中移除文件
    bool Close(const char* filepath);

    // 移除过期缓存条目
    void RemoveExpiredFileCache();

    // Getter
    int  GetMaxHeaderLength()   const;
    int  GetMaxFileSize()       const;
    int  GetStatInterval()      const;
    int  GetExpiredTime()       const;

    // Setter
    void SetMaxHeaderLength(int len);
    void SetMaxFileSize(int size);
};
```

## 使用示例

```c++
FileCacheEx filecache(200);  // 最大 200 个缓存条目
filecache.SetMaxHeaderLength(8192);  // 8K 头部预留
filecache.SetMaxFileSize(1 << 24);   // 16MB 最大缓存文件
filecache.stat_interval = 5;         // 每 5 秒检查文件变更
filecache.expired_time = 120;        // 2 分钟过期

FileCacheEx::OpenParam param;
file_cache_ex_ptr fc = filecache.Open("/var/www/index.html", &param);
if (fc) {
    // 写入 HTTP 响应头
    std::string header = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
    if (fc->prepend_header(header.c_str(), header.size())) {
        // 发送 fc->httpbuf (header + content)
    } else {
        // 头部超出预留空间，回退到普通发送
    }
}
```

## 迁移指南

从 `FileCache` 迁移到 `FileCacheEx`：

1. 替换头文件引用：`#include "FileCache.h"` → `#include "FileCacheEx.h"`
2. 替换类型：`FileCache` → `FileCacheEx`，`file_cache_ptr` → `file_cache_ex_ptr`
3. 处理 `prepend_header()` 的 `bool` 返回值
4. 可选：利用 `SetMaxHeaderLength()` / `SetMaxFileSize()` 配置参数

## 线程安全说明

- `FileCacheEx` 继承 `hv::LRUCache` 的全局互斥锁保护 LRU 操作
- 每个 `file_cache_ex_s` 条目内置 `std::mutex` 保护条目级修改
- `prepend_header()` 自动加锁，调用方无需额外同步
- `is_modified()` / `is_complete()` / `resize_buf()` 需要调用方持锁
