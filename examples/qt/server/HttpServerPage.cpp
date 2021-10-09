#include "HttpServerPage.h"

#include <QBoxLayout>

#include "mainwindow.h"

HttpServerPage::HttpServerPage(QWidget *parent) : QWidget(parent)
{
    server = nullptr;
    service = nullptr;
    ws = nullptr;
    initUI();
    initConnect();
}

HttpServerPage::~HttpServerPage()
{
    stop();
}

void HttpServerPage::initUI()
{
    QHBoxLayout* hbox = new QHBoxLayout;

    // host
    hbox->addWidget(new QLabel("host:"));
    hostEdt = new QLineEdit("0.0.0.0");
    hbox->addWidget(hostEdt);

    // port
    hbox->addWidget(new QLabel("port:"));
    portEdt = new QLineEdit("8080");
    hbox->addWidget(portEdt);

    // start
    startBtn = new QPushButton("start");
    hbox->addWidget(startBtn);

    // stop
    stopBtn = new QPushButton("stop");
    stopBtn->setEnabled(false);
    hbox->addWidget(stopBtn);

    setLayout(hbox);
}

void HttpServerPage::initConnect()
{
    connect(startBtn, &QPushButton::clicked, [this]() {
        std::string host = hostEdt->text().toStdString();
        int port = portEdt->text().toInt();

        if (start(port, host.c_str())) {
            startBtn->setEnabled(false);
            stopBtn->setEnabled(true);
            g_mainwnd->appendMessage(QString::asprintf("HTTP server running on %s:%d ...", host.c_str(), port));
        } else {
            g_mainwnd->appendMessage(QString::asprintf("HTTP server start failed!"));
        }
    });

    connect(stopBtn, &QPushButton::clicked, [this]() {
        stop();
        startBtn->setEnabled(true);
        stopBtn->setEnabled(false);
        g_mainwnd->appendMessage("HTTP server stopped!");
    });
}

bool HttpServerPage::start(int port, const char* host)
{
    service = new hv::HttpService;
    service->document_root = ".";
    service->home_page = "index.html";
    service->preprocessor = [](HttpRequest* req, HttpResponse* resp) {
        g_mainwnd->postMessage(QString("received http request:\n") + QString::fromStdString(req->Dump(true, true)));
        return 0;
    };
    service->POST("/echo", [](const HttpContextPtr& ctx) {
        // echo
        return ctx->send(ctx->body(), ctx->type());
    });
    service->postprocessor = [](HttpRequest* req, HttpResponse* resp) {
        g_mainwnd->postMessage(QString("send http response:\n") + QString::fromStdString(resp->Dump(true, true)));
        return 0;
    };

    ws = new hv::WebSocketService;
    ws->onopen = [](const WebSocketChannelPtr& channel, const std::string& url) {
        g_mainwnd->postMessage(QString("ws onopen: ") + QString::fromStdString(url));
    };
    ws->onmessage = [](const WebSocketChannelPtr& channel, const std::string& msg) {
        g_mainwnd->postMessage(QString("ws onmessage: ") + QString::fromStdString(msg));
        // echo
        channel->send(msg);
    };
    ws->onclose = [](const WebSocketChannelPtr& channel) {
        g_mainwnd->postMessage("ws onclose");
    };

    server = new hv::WebSocketServer;
    server->registerHttpService(service);
    server->registerWebSocketService(ws);
    server->setHost(host);
    server->setPort(port);
    server->setThreadNum(1);
    return server->start() == 0;
}

void HttpServerPage::stop()
{
    SAFE_DELETE(server);
    SAFE_DELETE(service);
    SAFE_DELETE(ws);
}
