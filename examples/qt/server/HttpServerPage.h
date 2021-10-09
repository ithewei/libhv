#ifndef HTTP_SERVER_PAGE_H
#define HTTP_SERVER_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "hv/HttpServer.h"
#include "hv/WebSocketServer.h"

class HttpServerPage : public QWidget
{
    Q_OBJECT
public:
    explicit HttpServerPage(QWidget *parent = nullptr);
    ~HttpServerPage();

    bool start(int port, const char* host = "0.0.0.0");
    void stop();

protected:
    void initUI();
    void initConnect();

private:
    QLineEdit *hostEdt;
    QLineEdit *portEdt;
    QPushButton *startBtn;
    QPushButton *stopBtn;

    // hv::HttpServer *server;
    hv::WebSocketServer *server;
    hv::HttpService *service;
    hv::WebSocketService *ws;
};

#endif // HTTP_SERVER_PAGE_H
