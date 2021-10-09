#ifndef UDP_CLIENT_PAGE_H
#define UDP_CLIENT_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "hv/UdpClient.h"

class UdpClientPage : public QWidget
{
    Q_OBJECT
public:
    explicit UdpClientPage(QWidget *parent = nullptr);
    ~UdpClientPage();

    bool start(int port, const char* host = "127.0.0.1");
    void stop();
    int send(const QString& msg);

protected:
    void initUI();
    void initConnect();

private:
    QLineEdit *hostEdt;
    QLineEdit *portEdt;
    QPushButton *startBtn;
    QPushButton *stopBtn;

    hv::UdpClient *client;
};

#endif // UDP_CLIENT_PAGE_H
