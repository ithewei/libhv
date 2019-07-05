#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include "HttpService.h"

class HttpServer {
public:
    HttpServer();

    int SetListenPort(int port);

    void SetWorkerProcesses(int worker_processes) {
        this->worker_processes = worker_processes;
    }

    void RegisterService(HttpService* service) {
        this->service = service;
    }

    void Run(bool wait = true);

public:
    int port;
    int worker_processes;
    HttpService* service;
    int listenfd_;
};

#endif
