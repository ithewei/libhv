#ifndef HV_EVENT_TLS_H_
#define HV_EVENT_TLS_H_

#include "hloop.h"

// Internal event-loop TLS handshake helpers. The caller installs the NIO
// readiness dispatcher before starting a handshake. These functions only
// manage the TLS transport phase and event mask; nio.c owns accept/connect
// completion and user callbacks.
int  tls_server_handshake_start(hio_t* listenio, hio_t* connio);
int  tls_client_handshake_start(hio_t* io);
void tls_handshake_step(hio_t* io);

#endif // HV_EVENT_TLS_H_
