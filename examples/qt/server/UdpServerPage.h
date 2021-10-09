#ifndef UDP_SERVER_PAGE_H
#define UDP_SERVER_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "hv/UdpServer.h"

class UdpServerPage : public QWidget
{
    Q_OBJECT
public:
    explicit UdpServerPage(QWidget *parent = nullptr);
    ~UdpServerPage();

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

    hv::UdpServer *server;
};

#endif // UDP_SERVER_PAGE_H
