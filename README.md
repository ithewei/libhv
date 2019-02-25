## Intro

hw 是一套跨平台c++工具集，类名以H开头

## platform

- gcc
- mingw
- msvc

## required

- c++11

## Module

- h.h：总头文件
- hversion.h: 版本
- hdef.h: 宏定义
- hplatform.h: 平台相关
- hendian.h: 大小端
- hlog.h: 日志
- herr.h: 错误码
- htime.h: 时间
- hstring.h: 字符串
- hfile.h: 文件类
- hthread.h：线程
- hthreadpool.h：线程池
- hmutex.h：同步锁
- hobj.h: 对象基类
- hvar.h: var变量
- hbuf.h: 缓存类
- hscope.h: 作用域RAII机制
- hmain.h: main_ctx: arg env
- hproc.h: 子进程/线程类
- hsysinfo.h: 系统信息
- hifconf.h: ifconfig实现
- singleton.h: 单例模式
- iniparser.h: ini解析

## other

- Makefile: 通用Makefile模板
- main.cpp.tmpl: 通用main.cpp模板

## BUILD

```
mv main.cpp.tmpl main.cpp
make DIRS="."
```
