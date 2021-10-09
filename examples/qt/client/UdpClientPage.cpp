#include "UdpClientPage.h"

#include <QBoxLayout>

#include "mainwindow.h"

UdpClientPage::UdpClientPage(QWidget *parent) : QWidget(parent)
{
    client = nullptr;
    initUI();
    initConnect();
}

UdpClientPage::~UdpClientPage()
{
    stop();
}

void UdpClientPage::initUI()
{
    QHBoxLayout* hbox = new QHBoxLayout;

    // host
    hbox->addWidget(new QLabel("host:"));
    hostEdt = new QLineEdit("127.0.0.1");
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

void UdpClientPage::initConnect()
{
    connect(startBtn, &QPushButton::clicked, [this]() {
        std::string host = hostEdt->text().toStdString();
        int port = portEdt->text().toInt();

        if (start(port, host.c_str())) {
            startBtn->setEnabled(false);
            stopBtn->setEnabled(true);
            g_mainwnd->appendMessage(QString::asprintf("UDP client sendto %s:%d ...", host.c_str(), port));
        } else {
            g_mainwnd->appendMessage(QString::asprintf("UDP client start failed!"));
        }
    });

    connect(stopBtn, &QPushButton::clicked, [this]() {
        stop();
        startBtn->setEnabled(true);
        stopBtn->setEnabled(false);
        g_mainwnd->appendMessage("UDP client stopped!");
    });
}

bool UdpClientPage::start(int port, const char* host)
{
    client = new hv::UdpClient;
    int sockfd = client->createsocket(port, host);
    if (sockfd < 0) {
        return false;
    }
    client->onMessage = [](const hv::SocketChannelPtr& channel, hv::Buffer* buf) {
        g_mainwnd->postMessage(QString::asprintf("< %.*s", (int)buf->size(), (char*)buf->data()));
    };
    client->start();
    return true;
}

void UdpClientPage::stop()
{
    SAFE_DELETE(client);
}

int UdpClientPage::send(const QString& msg)
{
    if (client == nullptr) {
        g_mainwnd->postMessage("Please start first!");
        return -1;
    }
    return client->sendto(msg.toStdString());
}
