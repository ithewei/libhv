#include "mainwindow.h"
#include "customevent.h"

#include <QApplication>
#include <QDateTime>

#include <QBoxLayout>

#include "TcpClientPage.h"
#include "UdpClientPage.h"
#include "HttpClientPage.h"
#include "WebSocketClientPage.h"

SINGLETON_IMPL(MainWindow)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    initUI();
    initConnect();
}

MainWindow::~MainWindow()
{
}

void MainWindow::initUI()
{
    initMenu();

    setFixedSize(800, 600);

    QVBoxLayout* vbox = new QVBoxLayout();

    tab = new QTabWidget;
    tab->addTab(new TcpClientPage, "TCP");
    tab->addTab(new UdpClientPage, "UDP");
    tab->addTab(new HttpClientPage, "HTTP");
    tab->addTab(new WebSocketClientPage, "WebSocket");
    vbox->addWidget(tab);

    QHBoxLayout* hbox = new QHBoxLayout();
    sendmsg = new QTextEdit("hello");
    sendmsg->setReadOnly(false);
    hbox->addWidget(sendmsg);
    sendbtn = new QPushButton("send");
    hbox->addWidget(sendbtn);
    vbox->addLayout(hbox);

    recvmsg = new QTextEdit();
    recvmsg->setReadOnly(true);
    vbox->addWidget(recvmsg);

    center = new QWidget;
    center->setLayout(vbox);

    setCentralWidget(center);
}

void MainWindow::initMenu()
{

}

void MainWindow::initConnect()
{
    connect(sendbtn, &QPushButton::clicked, [this]() {
        QWidget* page = tab->currentWidget();
        QString msg = sendmsg->toPlainText();
        switch (tab->currentIndex()) {
        case 0:
            {
                TcpClientPage *client = dynamic_cast<TcpClientPage*>(page);
                client->send(msg);
            }
            break;
        case 1:
            {
                UdpClientPage *client = dynamic_cast<UdpClientPage*>(page);
                client->send(msg);
            }
            break;
        case 2:
            {
                HttpClientPage *client = dynamic_cast<HttpClientPage*>(page);
                client->send(msg);
            }
            break;
        case 3:
            {
                WebSocketClientPage *client = dynamic_cast<WebSocketClientPage*>(page);
                client->send(msg);
            }
            break;
        default:
            break;
        }
    });
}

void MainWindow::postMessage(const QString &msg)
{
    QStringEvent* event = new QStringEvent(msg);
    QApplication::postEvent(this, event);
}

void MainWindow::appendMessage(const QString& msg)
{
    QString text = recvmsg->toPlainText();
    text += QDateTime::currentDateTime().toString("[yyyy-MM-dd hh:mm:ss.zzz] ");
    text += msg;
    if (text.back() != '\n') {
        text += "\n";
    }
    showMessage(text);
}

void MainWindow::showMessage(const QString& msg)
{
    recvmsg->setText(msg);
    QTextCursor cursor = recvmsg->textCursor();
    cursor.movePosition(QTextCursor::End);
    recvmsg->setTextCursor(cursor);
    recvmsg->repaint();
}

void MainWindow::customEvent(QEvent* e)
{
    switch(e->type())
    {
    case qEventRecvMsg:
        {
            QStringEvent* event = dynamic_cast<QStringEvent*>(e);
            appendMessage(event->message);
        }
        e->accept();
        break;
    default:
        break;
    }
}
