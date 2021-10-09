#ifndef TCP_SERVER_PAGE_H
#define TCP_SERVER_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "hv/TcpServer.h"

class TcpServerPage : public QWidget
{
    Q_OBJECT
public:
    explicit TcpServerPage(QWidget *parent = nullptr);
    ~TcpServerPage();

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

    hv::TcpServer *server;
};

#endif // TCP_SERVER_PAGE_H
