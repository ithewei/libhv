#include "TcpServerPage.h"

#include <QBoxLayout>

#include "mainwindow.h"

TcpServerPage::TcpServerPage(QWidget *parent) : QWidget(parent)
{
    server = nullptr;
    initUI();
    initConnect();
}

TcpServerPage::~TcpServerPage()
{
    stop();
}

void TcpServerPage::initUI()
{
    QHBoxLayout* hbox = new QHBoxLayout;

    // host
    hbox->addWidget(new QLabel("host:"));
    hostEdt = new QLineEdit("0.0.0.0");
    hbox->addWidget(hostEdt);

    // port
    hbox->addWidget(new QLabel("port:"));
    portEdt = new QLineEdit("1234");
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

void TcpServerPage::initConnect()
{
    connect(startBtn, &QPushButton::clicked, [this]() {
        std::string host = hostEdt->text().toStdString();
        int port = portEdt->text().toInt();

        if (start(port, host.c_str())) {
            startBtn->setEnabled(false);
            stopBtn->setEnabled(true);
            g_mainwnd->appendMessage(QString::asprintf("TCP server running on %s:%d ...", host.c_str(), port));
        } else {
            g_mainwnd->appendMessage(QString::asprintf("TCP server start failed!"));
        }
    });

    connect(stopBtn, &QPushButton::clicked, [this]() {
        stop();
        startBtn->setEnabled(true);
        stopBtn->setEnabled(false);
        g_mainwnd->appendMessage("TCP server stopped!");
    });
}

bool TcpServerPage::start(int port, const char* host)
{
    server = new hv::TcpServer;
    int listenfd = server->createsocket(port, host);
    if (listenfd < 0) {
        return false;
    }
    server->setThreadNum(1);
    server->onConnection = [](const hv::SocketChannelPtr& channel) {
        std::string peeraddr = channel->peeraddr();
        if (channel->isConnected()) {
            g_mainwnd->postMessage(QString::asprintf("%s connected! connfd=%d", peeraddr.c_str(), channel->fd()));
        } else {
            g_mainwnd->postMessage(QString::asprintf("%s disconnected! connfd=%d", peeraddr.c_str(), channel->fd()));
        }
    };
    server->onMessage = [](const hv::SocketChannelPtr& channel, hv::Buffer* buf) {
        g_mainwnd->postMessage(QString::asprintf("< %.*s", (int)buf->size(), (char*)buf->data()));
        // echo
        channel->write(buf);
    };
    server->start();
    return true;
}

void TcpServerPage::stop()
{
    SAFE_DELETE(server);
}
