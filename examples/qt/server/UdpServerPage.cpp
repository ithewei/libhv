#include "UdpServerPage.h"

#include <QBoxLayout>

#include "mainwindow.h"

UdpServerPage::UdpServerPage(QWidget *parent) : QWidget(parent)
{
    server = nullptr;
    initUI();
    initConnect();
}

UdpServerPage::~UdpServerPage()
{
    stop();
}

void UdpServerPage::initUI()
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

void UdpServerPage::initConnect()
{
    connect(startBtn, &QPushButton::clicked, [this]() {
        std::string host = hostEdt->text().toStdString();
        int port = portEdt->text().toInt();

        if (start(port, host.c_str())) {
            startBtn->setEnabled(false);
            stopBtn->setEnabled(true);
            g_mainwnd->appendMessage(QString::asprintf("UDP server running on %s:%d ...", host.c_str(), port));
        } else {
            g_mainwnd->appendMessage(QString::asprintf("UDP server start failed!"));
        }
    });

    connect(stopBtn, &QPushButton::clicked, [this]() {
        stop();
        startBtn->setEnabled(true);
        stopBtn->setEnabled(false);
        g_mainwnd->appendMessage("UDP server stopped!");
    });
}

bool UdpServerPage::start(int port, const char* host)
{
    server = new hv::UdpServer;
    int bindfd = server->createsocket(port, host);
    if (bindfd < 0) {
        return false;
    }
    server->onMessage = [](const hv::SocketChannelPtr& channel, hv::Buffer* buf) {
        g_mainwnd->postMessage(QString::asprintf("< %.*s", (int)buf->size(), (char*)buf->data()));
        // echo
        channel->write(buf);
    };
    server->start();
    return true;
}

void UdpServerPage::stop()
{
    SAFE_DELETE(server);
}
