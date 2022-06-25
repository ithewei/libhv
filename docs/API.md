# libhv API Manual

## base

### hplatform.h
- OS: OS_WIN, OS_UNIX (OS_LINUX, OS_ANDROID, OS_DARWIN ...)
- ARCH: ARCH_X86, ARCH_X64, ARCH_ARM, ARCH_ARM64
- COMPILER: COMPILER_MSVC, COMPILER_MINGW, COMPILER_GCC, COMPILER_CLANG
- BYTE_ORDER: BIG_ENDIAN, LITTLE_ENDIAN
- stdbool.h: bool, true, false
- stdint.h: int8_t, int16_t, int32_t, int64_t
- hv_sleep, hv_msleep, hv_usleep, hv_delay
- hv_mkdir
- stricmp, strcasecmp

### hexport.h
- HV_EXPORT, HV_INLINE
- HV_SOURCE, HV_STATICLIB, HV_DYNAMICLIB
- HV_DEPRECATED
- HV_UNUSED
- EXTERN_C, BEGIN_EXTERN_C, END_EXTERN_C
- BEGIN_NAMESPACE, END_NAMESPACE, USING_NAMESPACE
- DEFAULT
- ENUM, STRUCT
- IN, OUT, INOUT
- OPTIONAL, REQUIRED, REPEATED

### hdef.h
- ABS, NABS
- ARRAY_SIZE
- BITSET, BITCLR, BITGET
- CR, LF, CRLF
- FLOAT_EQUAL_ZERO
- INFINITE
- IS_ALPHA, IS_NUM, IS_ALPHANUM
- IS_CNTRL, IS_GRAPH
- IS_HEX
- IS_LOWER, IS_UPPER
- LOWER, UPPER
- MAKEWORD, LOBYTE, HIBYTE
- MAKELONG, LOWORD, HIWORD
- MAKEINT64, LOINT, HIINT
- MAKE_FOURCC
- MAX, MIN, LIMIT
- MAX_PATH
- NULL, TRUE, FALSE
- SAFE_FREE, SAFE_DELETE, SAFE_DELETE_ARRAY, SAFE_RELEASE
- STRINGCAT
- STRINGIFY
- offsetof, offsetofend
- container_of
- prefetch
- printd, printe

### hatomic.h
- hatomic_flag_t, hatomic_t
- hatomic_flag_test_and_set
- hatomic_flag_clear
- hatomic_add
- hatomic_sub
- hatomic_inc
- hatomic_dec

### herr.h
- hv_strerror

### htime.h
- IS_LEAP_YEAR
- datetime_t
- gettick_ms
- gettimeofday
- gettimeofday_ms
- gettimeofday_us
- gethrtime_us
- datetime_now
- datetime_mktime
- datetime_past
- datetime_future
- duration_fmt
- datetime_fmt
- gmtime_fmt
- days_of_month
- month_atoi
- month_itoa
- weekday_atoi
- weekday_itoa
- hv_compile_datetime
- cron_next_timeout

### hmath.h
- floor2e
- ceil2e
- varint_encode
- varint_decode

### hbase.h
- hv_malloc
- hv_calloc
- hv_realloc
- hv_zalloc
- hv_strncpy
- hv_strncat
- hv_strlower
- hv_strupper
- hv_strreverse
- hv_strstartswith
- hv_strendswith
- hv_strcontains
- hv_strnchr
- hv_strrchr_dot
- hv_strrchr_dir
- hv_basename
- hv_suffixname
- hv_mkdir_p
- hv_rmdir_p
- hv_exists
- hv_isdir
- hv_isfile
- hv_islink
- hv_filesize
- get_executable_path
- get_executable_dir
- get_executable_file
- get_run_dir
- hv_rand
- hv_random_string
- hv_getboolean
- hv_parse_size
- hv_parse_time
- hv_parse_url

### hversion.h
- hv_version
- hv_compile_version
- version_atoi
- version_itoa

### hsysinfo.h
- get_ncpu
- get_meminfo

### hproc.h
- hproc_spawn

### hthread.h
- hv_getpid
- hv_gettid
- HTHREAD_RETTYPE
- HTHREAD_ROUTINE
- hthread_create
- hthread_join
- class HThread

### hmutex.h
- hmutex_t
- hmutex_init
- hmutex_destroy
- hmutex_lock
- hmutex_unlock
- hspinlock_t
- hspinlock_init
- hspinlock_destroy
- hspinlock_lock
- hspinlock_unlock
- hrwlock_t
- hrwlock_init
- hrwlock_destroy
- hrwlock_rdlock
- hrwlock_rdunlock
- hrwlock_wrlock
- hrwlock_wrunlock
- htimed_mutex_t
- htimed_mutex_init
- htimed_mutex_destroy
- htimed_mutex_lock
- htimed_mutex_lock_for
- htimed_mutex_unlock
- hcondvar_t
- hcondvar_init
- hcondvar_destroy
- hcondvar_wait
- hcondvar_wait_for
- hcondvar_signal
- hcondvar_broadcast
- hsem_init
- hsem_destroy
- hsem_wait
- hsem_post
- hsem_timedwait
- honce_t
- HONCE_INIT
- honce
- class `hv::MutexLock`
- class `hv::SpinLock`
- class `hv::RWLock`
- class `hv::LockGuard`
- synchronized

### hsocket.h
- INVALID_SOCKET
- closesocket
- blocking
- nonblocking
- Bind
- Listen
- Connect
- ConnectNonblock
- ConnectTimeout
- ResolveAddr
- Socketpair
- socket_errno
- socket_strerror
- sockaddr_u
- sockaddr_ip
- sockaddr_port
- sockaddr_set_ip
- sockaddr_set_port
- sockaddr_set_ipport
- sockaddr_len
- sockaddr_str
- sockaddr_print
- SOCKADDR_LEN
- SOCKADDR_STR
- SOCKADDR_PRINT
- tcp_nodelay
- tcp_nopush
- tcp_keepalive
- udp_broadcast
- so_sndtimeo
- so_rcvtimeo

### hlog.h
- default_logger
- file_logger
- stderr_logger
- stdout_logger
- logger_create
- logger_destroy
- logger_enable_color
- logger_enable_fsync
- logger_fsync
- logger_print
- logger_set_file
- logger_set_handler
- logger_set_level
- logger_set_max_bufsize
- logger_set_max_filesize
- logger_set_remain_days
- logger_get_cur_file
- hlogd, hlogi, hlogw, hloge, hlogf
- LOGD, LOGI, LOGW, LOGE, LOGF

### hbuf.h
- hbuf_t
- offset_buf_t
- class HBuf
- class HVLBuf
- class HRingBuf

### hmain.h
- main_ctx_init
- parse_opt
- parse_opt_long
- get_arg
- get_env
- setproctitle
- signal_init
- signal_handle
- create_pidfile
- delete_pidfile
- getpid_form_pidfile
- master_workers_run

### hstring.h
- to_string
- from_string
- toupper
- tolower
- reverse
- startswith
- endswith
- contains
- asprintf
- trim
- ltrim
- rtrim
- trim_pairs
- split
- splitKV
- replace
- replaceAll

### hfile.h
- class HFile

### hpath.h
- exists
- isdir
- isfile
- islink
- basename
- dirname
- filename
- suffixname
- join

### hdir.h
- listdir

### hurl.h
- HUrl::escape
- HUrl::unescape
- HUrl::parse
- HUrl::dump

### hscope.h
- defer
- template ScopeCleanup
- template ScopeFree
- template ScopeDelete
- template ScopeDeleteArray
- template ScopeRelease
- template ScopeLock

### ifconfig.h
- ifconfig

## utils
### md5.h
- HV_MD5Init
- HV_MD5Update
- HV_MD5Final
- hv_md5
- hv_md5_hex

### sha1.h
- HV_SHA1Init
- HV_SHA1Update
- HV_SHA1Final
- HV_SHA1
- hv_sha1
- hv_sha1_hex

### base64.h
- hv_base64_decode
- hv_base64_encode

### json.hpp
- json::parse
- json::dump

### singleton.h
- DISABLE_COPY
- SINGLETON_DECL
- SINGLETON_IMPL

## event

### hloop.h
- hloop_create_tcp_client
- hloop_create_tcp_server
- hloop_create_udp_client
- hloop_create_udp_server
- hloop_create_ssl_client
- hloop_create_ssl_server
- hloop_new
- hloop_free
- hloop_run
- hloop_stop
- hloop_pause
- hloop_resume
- hloop_status
- hloop_pid
- hloop_tid
- hloop_now
- hloop_now_ms
- hloop_now_us
- hloop_update_time
- hloop_set_userdata
- hloop_userdata
- hloop_wakeup
- hloop_post_event
- hevent_loop
- hevent_type
- hevent_id
- hevent_priority
- hevent_userdata
- hevent_set_priority
- hevent_ser_userdata
- haccept
- hconnect
- hread
- hwrite
- hrecv
- hsend
- hrecvfrom
- hsendto
- hio_add
- hio_del
- hio_get
- hio_detach
- hio_attach
- hio_read
- hio_read_start
- hio_read_stop
- hio_read_once
- hio_read_until
- hio_read_until_length
- hio_read_until_delim
- hio_read_readline
- hio_read_readstring
- hio_read_readbytes
- hio_write
- hio_close
- hio_accept
- hio_connect
- hio_fd
- hio_id
- hio_type
- hio_error
- hio_localaddr
- hio_peeraddr
- hio_events
- hio_revents
- hio_is_opened
- hio_is_closed
- hio_enable_ssl
- hio_is_ssl
- hio_get_ssl
- hio_set_ssl
- hio_get_ssl_ctx
- hio_set_ssl_ctx
- hio_new_ssl_ctx
- hio_setcb_accept
- hio_setcb_connect
- hio_setcb_read
- hio_setcb_write
- hio_setcb_close
- hio_getcb_accept
- hio_getcb_connect
- hio_getcb_read
- hio_getcb_write
- hio_getcb_close
- hio_set_type
- hio_set_localaddr
- hio_set_peeraddr
- hio_set_readbuf
- hio_set_connect_timeout
- hio_set_close_timeout
- hio_set_read_timeout
- hio_set_write_timeout
- hio_set_keepalive_timeout
- hio_set_heartbeat
- hio_set_unpack
- hio_unset_unpack
- hio_read_upstream
- hio_write_upstream
- hio_close_upstream
- hio_setup_upstream
- hio_get_upstream
- hio_setup_tcp_upstream
- hio_setup_ssl_upstream
- hio_setup_udp_upstream
- hio_create_socket
- hio_context
- hio_set_context
- htimer_add
- htimer_add_period
- htimer_del
- htimer_reset
- hidle_add
- hidle_del

### nlog.h
- network_logger
- nlog_listen

## evpp
- class Buffer
- class Channel
- class Event
- class EventLoop
- class EventLoopThread
- class EventLoopThreadPool
- class TcpClient
- class TcpServer
- class UdpClient
- class UdpServer

## ssl
- hssl_ctx_init
- hssl_ctx_cleanup
- hssl_ctx_instance
- hssl_ctx_new
- hssl_ctx_free
- hssl_new
- hssl_free
- hssl_accept
- hssl_connnect
- hssl_read
- hssl_write
- hssl_close
- hssl_set_sni_hostname

## protocol

### dns.h
- dns_name_decode
- dns_name_encode
- dns_pack
- dns_unpack
- dns_rr_pack
- dns_rr_unpack
- dns_query
- dns_free
- nslookup

### ftp.h
- ftp_command_str
- ftp_connect
- ftp_login
- ftp_exec
- ftp_upload
- ftp_download
- ftp_download_with_cb
- ftp_quit
- ftp_status_str

### smtp.h
- smtp_command_str
- smtp_status_str
- smtp_build_command
- sendmail

### icmp.h
- ping

## http
- class HttpMessage
- class HttpRequest
- class HttpResponse
- class HttpParser
- class HttpService

### httpdef.h
- http_content_type_enum
- http_content_type_enum_by_suffix
- http_content_type_str
- http_content_type_str_by_suffix
- http_content_type_suffix
- http_errno_description
- http_errno_name
- http_method_enum
- http_method_str
- http_status_enum
- http_status_str

### http_content.h
- parse_query_params
- parse_json
- parse_multipart
- dump_query_params
- dump_json
- dump_multipart

### http_client.h
- http_client_new
- http_client_del
- http_client_send
- http_client_send_async
- http_client_strerror
- http_client_set_timeout
- http_client_set_header
- http_client_del_header
- http_client_get_header
- http_client_clear_headers
- http_client_set_http_proxy
- http_client_set_https_proxy
- http_client_add_no_proxy
- class HttpClient

### requests.h
- requests::request
- requests::get
- requests::post
- requests::put
- requests::patch
- requests::Delete
- requests::head
- requests::async

### axios.h
- axios::axios
- axios::get
- axios::post
- axios::put
- axios::patch
- axios::Delete
- axios::head
- axios::async

### HttpServer.h
- http_server_run
- http_server_stop
- class HttpServer

### WebSocketClient.h
- class WebSocketClient

### WebSocketServer.h
- websocket_server_run
- websocket_server_stop
- class WebSocketServer

## mqtt
- mqtt_client_new
- mqtt_client_free
- mqtt_client_run
- mqtt_client_stop
- mqtt_client_set_id
- mqtt_client_set_will
- mqtt_client_set_auth
- mqtt_client_set_callback
- mqtt_client_set_userdata
- mqtt_client_get_userdata
- mqtt_client_get_last_error
- mqtt_client_set_ssl_ctx
- mqtt_client_new_ssl_ctx
- mqtt_client_set_reconnect
- mqtt_client_reconnect
- mqtt_client_set_connect_timeout
- mqtt_client_connect
- mqtt_client_is_connected
- mqtt_client_disconnect
- mqtt_client_publish
- mqtt_client_subscribe
- mqtt_client_unsubscribe

## other
- class HThreadPool
- class HObjectPool
- class ThreadLocalStorage
