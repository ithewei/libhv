#include "mainwindow.h"
#include "customevent.h"

#include <QApplication>
#include <QDateTime>

#include <QBoxLayout>

#include "TcpServerPage.h"
#include "UdpServerPage.h"
#include "HttpServerPage.h"

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
    tab->addTab(new TcpServerPage, "TCP");
    tab->addTab(new UdpServerPage, "UDP");
    tab->addTab(new HttpServerPage, "HTTP | WebSocket");
    vbox->addWidget(tab);

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
