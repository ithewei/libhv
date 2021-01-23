## 目录结构

```
.
├── base        libhv c/c++基础设施模块，如常用宏定义、数据结构、字符串操作、日期时间、文件、目录、线程、进程、日志、套接字
├── bin         可执行文件安装目录
├── build       cmake默认构建目录
├── cert        SSL证书存放目录
├── cmake       cmake脚本存放目录
├── consul      consul服务注册与发现，使用http客户端实现
├── docs        文档存放目录
├── echo-servers 包含libevent、libev、libuv、libhv、asio、poco、muduo等多个网络库的tcp echo server写法，并做压力测试
├── etc         应用程序配置目录
├── event       libhv事件循环模块
├── evpp        事件循环c++封装类
├── examples    示例代码
│   └── httpd
├── html        网页document_root目录
│   ├── downloads   下载目录
│   └── uploads     上传目录
├── http        libhv http模块
│   ├── client
│   └── server
├── include     头文件安装目录
│   └── hv
├── lib         库文件安装目录
├── logs        日志生成目录
├── misc        杂项
├── protocol    包含icmp、dns、ftp、smtp等协议的实现
├── scripts     shell脚本存放目录
├── unittest    单元测试代码
└── utils       libhv utils模块，如base64、md5、json解析、ini解析

```
