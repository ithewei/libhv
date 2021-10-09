#include "WebSocketClientPage.h"
#include "customevent.h"

#include <QApplication>
#include <QBoxLayout>

#include "mainwindow.h"

WebSocketClientPage::WebSocketClientPage(QWidget *parent) : QWidget(parent)
{
    client = nullptr;
    initUI();
    initConnect();
}

WebSocketClientPage::~WebSocketClientPage()
{
    close();
}

void WebSocketClientPage::initUI()
{
    QHBoxLayout* hbox = new QHBoxLayout;

    // url
    hbox->addWidget(new QLabel("url:"));
    urlEdt = new QLineEdit("ws://127.0.0.1:8080/echo");
    hbox->addWidget(urlEdt);

    // open
    openBtn = new QPushButton("open");
    hbox->addWidget(openBtn);

    // close
    closeBtn = new QPushButton("close");
    closeBtn->setEnabled(false);
    hbox->addWidget(closeBtn);

    setLayout(hbox);
}

void WebSocketClientPage::initConnect()
{
    connect(openBtn, &QPushButton::clicked, [this]() {
        std::string url = urlEdt->text().toStdString();

        if (open(url.c_str())) {
            openBtn->setEnabled(false);
            closeBtn->setEnabled(true);
            g_mainwnd->appendMessage(QString::asprintf("WS client openning to %s ...", url.c_str()));
        } else {
            g_mainwnd->appendMessage(QString::asprintf("WS client open failed!"));
        }
    });

    connect(closeBtn, &QPushButton::clicked, [this]() {
        close();
        openBtn->setEnabled(true);
        closeBtn->setEnabled(false);
        g_mainwnd->appendMessage("WS client closing ...");
    });
}

void WebSocketClientPage::customEvent(QEvent* e)
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
    case qEventOpened:
        {
            QStringEvent* event = dynamic_cast<QStringEvent*>(e);
            openBtn->setEnabled(false);
            closeBtn->setEnabled(true);
            g_mainwnd->appendMessage(event->message);
        }
        e->accept();
        break;
    case qEventClosed:
        {
            QStringEvent* event = dynamic_cast<QStringEvent*>(e);
            openBtn->setEnabled(true);
            closeBtn->setEnabled(false);
            g_mainwnd->appendMessage(event->message);
        }
        e->accept();
    break;
    default:
        break;
    }
}

bool WebSocketClientPage::open(const char* url)
{
    client = new hv::WebSocketClient;
    client->onopen = [this]() {
        QStringEvent* event = new QStringEvent("WS client opened!", qEventOpened);
        QApplication::postEvent(this, event);
    };
    client->onclose = [this]() {
        QStringEvent* event = new QStringEvent("WS client closed!", qEventClosed);
        QApplication::postEvent(this, event);
    };
    client->onmessage = [](const std::string& msg) {
        g_mainwnd->postMessage(QString("< ") + QString::fromStdString(msg));
    };
    return client->open(url) == 0;
}

void WebSocketClientPage::close()
{
    SAFE_DELETE(client);
}

int WebSocketClientPage::send(const QString& msg)
{
    if (client == nullptr || !client->isConnected()) {
        g_mainwnd->postMessage("Please open first!");
        return -1;
    }
    return client->send(msg.toStdString());
}
