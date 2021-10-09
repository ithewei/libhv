#include "TcpClientPage.h"
#include "customevent.h"

#include <QApplication>
#include <QBoxLayout>

#include "mainwindow.h"

TcpClientPage::TcpClientPage(QWidget *parent) : QWidget(parent)
{
    client = nullptr;
    initUI();
    initConnect();
}

TcpClientPage::~TcpClientPage()
{
    close();
}

void TcpClientPage::initUI()
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

    // connect
    connectBtn = new QPushButton("connect");
    hbox->addWidget(connectBtn);

    // close
    closeBtn = new QPushButton("close");
    closeBtn->setEnabled(false);
    hbox->addWidget(closeBtn);

    setLayout(hbox);
}

void TcpClientPage::initConnect()
{
    QObject::connect(connectBtn, &QPushButton::clicked, [this]() {
        std::string host = hostEdt->text().toStdString();
        int port = portEdt->text().toInt();

        if (connect(port, host.c_str())) {
            connectBtn->setEnabled(false);
            closeBtn->setEnabled(true);
            g_mainwnd->appendMessage(QString::asprintf("TCP client connecting to %s:%d ...", host.c_str(), port));
        } else {
            g_mainwnd->appendMessage(QString::asprintf("TCP client connect failed!"));
        }
    });

    QObject::connect(closeBtn, &QPushButton::clicked, [this]() {
        close();
        connectBtn->setEnabled(true);
        closeBtn->setEnabled(false);
        g_mainwnd->appendMessage("TCP client closing ...");
    });
}

void TcpClientPage::customEvent(QEvent* e)
{
    switch(e->type())
    {
    case qEventRecvMsg:
        {
            QStringEvent* event = dynamic_cast<QStringEvent*>(e);
            g_mainwnd->appendMessage(event->message);
        }
        e->accept();
        break;
    case qEventConnected:
        {
            QStringEvent* event = dynamic_cast<QStringEvent*>(e);
            connectBtn->setEnabled(false);
            closeBtn->setEnabled(true);
            g_mainwnd->appendMessage(event->message);
        }
        e->accept();
        break;
    case qEventDisconnected:
        {
            QStringEvent* event = dynamic_cast<QStringEvent*>(e);
            connectBtn->setEnabled(true);
            closeBtn->setEnabled(false);
            g_mainwnd->appendMessage(event->message);
        }
        e->accept();
    break;
    default:
        break;
    }
}

bool TcpClientPage::connect(int port, const char *host)
{
    client = new hv::TcpClient;
    int connfd = client->createsocket(port, host);
    if (connfd < 0) {
        return false;
    }
    client->onConnection = [this](const hv::SocketChannelPtr& channel) {
        QStringEvent* event;
        if (channel->isConnected()) {
            event = new QStringEvent(QString::asprintf("TCP client connected! connfd=%d", channel->fd()), qEventConnected);
        } else {
            event = new QStringEvent(QString::asprintf("TCP client disconnected! connfd=%d", channel->fd()), qEventDisconnected);
        }
        QApplication::postEvent(this, event);
    };
    client->onMessage = [](const hv::SocketChannelPtr& channel, hv::Buffer* buf) {
        g_mainwnd->postMessage(QString::asprintf("< %.*s", (int)buf->size(), (char*)buf->data()));
    };
    client->start();
    return true;
}

void TcpClientPage::close()
{
    SAFE_DELETE(client);
}

int TcpClientPage::send(const QString& msg)
{
    if (client == nullptr || !client->isConnected()) {
        g_mainwnd->postMessage("Please connect first!");
        return -1;
    }
    return client->send(msg.toStdString());
}
