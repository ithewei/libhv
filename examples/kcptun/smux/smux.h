#ifndef HV_SMUX_H_
#define HV_SMUX_H_

/*
 * smux: Simple MUltipleXing used by kcptun
 * @see: https://github.com/xtaci/smux
 *
 */

#include <map>

#include "hplatform.h"
#include "hbase.h"
#include "hbuf.h"
#include "hloop.h"

typedef enum {
    // v1
    SMUX_CMD_SYN = 0, // stream open
    SMUX_CMD_FIN = 1, // stream close
    SMUX_CMD_PSH = 2, // data push
    SMUX_CMD_NOP = 3, // no operation
    // v2
    SMUX_CMD_UPD = 4, // update
} smux_cmd_e;

typedef struct {
    uint8_t     version;
    uint8_t     cmd;
    uint16_t    length;
    uint32_t    sid;
} smux_head_t;

#define SMUX_HEAD_LENGTH    8

typedef struct {
    smux_head_t head;
    const char* data;
} smux_frame_t;

static inline unsigned int smux_package_length(const smux_head_t* head) {
    return SMUX_HEAD_LENGTH + head->length;
}

static inline void smux_head_init(smux_head_t* head) {
    head->version = 1;
    head->cmd = (uint8_t)SMUX_CMD_PSH;
    head->length = 0;
    head->sid = 0;
}

static inline void smux_frame_init(smux_frame_t* frame) {
    smux_head_init(&frame->head);
    frame->data = NULL;
}

// @retval >0 package_length, <0 error
int smux_frame_pack(const smux_frame_t* frame, void* buf, int len);
// @retval >0 package_length, <0 error
int smux_frame_unpack(smux_frame_t* frame, const void* buf, int len);

typedef struct smux_config_s {
    int version;
    int keepalive_interval;
    int keepalive_timeout;
    int max_frame_size;

    smux_config_s() {
        version = 1;
        keepalive_interval = 10000;
        keepalive_timeout  = 30000;
        max_frame_size = 1024;
    }
} smux_config_t;

typedef struct {
    uint32_t        stream_id;
    smux_frame_t    frame;
    hbuf_t          rbuf;
    hbuf_t          wbuf;
    hio_t*          io;
    htimer_t*       timer;
} smux_stream_t;

// @retval >0 package_length, <0 error, data => wbuf
static inline int smux_stream_output(smux_stream_t* stream, smux_frame_t* frame) {
    return smux_frame_pack(frame, stream->wbuf.base, stream->wbuf.len);
}

static inline int smux_stream_output(smux_stream_t* stream, smux_cmd_e cmd) {
    smux_frame_t frame;
    smux_frame_init(&frame);
    frame.head.sid = stream->stream_id;
    frame.head.cmd = (uint8_t)cmd;
    return smux_frame_pack(&frame, stream->wbuf.base, stream->wbuf.len);
}

// @retval >0 package_length, <0 error, data => frame
static inline int smux_stream_input(smux_stream_t* stream, const void* buf, int len) {
    return smux_frame_unpack(&stream->frame, buf, len);
}

typedef struct {
    uint32_t                            next_stream_id;
    // stream_id => smux_stream_t
    std::map<uint32_t, smux_stream_t*>  streams;
} smux_session_t;

static inline smux_stream_t* smux_session_open_stream(smux_session_t* session, uint32_t stream_id = 0, hio_t* io = NULL) {
    smux_stream_t* stream = NULL;
    HV_ALLOC_SIZEOF(stream);
    if (stream_id == 0) {
        session->next_stream_id += 2;
        stream_id = session->next_stream_id;
    }
    stream->stream_id = stream_id;
    session->streams[stream_id] = stream;
    stream->io = io;
    return stream;
}

static inline smux_stream_t* smux_session_get_stream(smux_session_t* session, uint32_t stream_id) {
    auto iter = session->streams.find(stream_id);
    if (iter != session->streams.end()) {
        return iter->second;
    }
    return NULL;
}

static inline void smux_session_close_stream(smux_session_t* session, uint32_t stream_id) {
    auto iter = session->streams.find(stream_id);
    if (iter != session->streams.end()) {
        HV_FREE(iter->second);
        session->streams.erase(iter);
    }
}

#endif // HV_SMUX_H_
