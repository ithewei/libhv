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
- hplatform.h: 平台相关
- hdef.h: 宏定义
- hversion.h: 版本
- hsysinfo.h: 系统信息
- hproc.h: 子进程/线程类
- htime.h: 时间
- herr.h: 错误码
- hlog.h: 日志
- hstring.h: 字符串
- hvar.h: var变量
- hobj.h: 对象基类
- hgui.h: gui相关定义
- hbuf.h: 缓存类
- hfile.h: 文件类
- hscope.h: 作用域RAII机制
- hmutex.h：同步锁
- hthread.h：线程
- hthreadpool.h：线程池
- hendian.h: 大小端
- hmain.h: main_ctx: arg env
- ifconfig.h: ifconfig实现
- singleton.h: 单例模式
- iniparser.h: ini解析

## other

- Makefile.in: 通用Makefile模板
- main.cpp.tmpl: 通用main.cpp模板

## BUILD

```
make test
```
