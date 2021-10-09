#ifndef TCP_CLIENT_PAGE_H
#define TCP_CLIENT_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "hv/TcpClient.h"

class TcpClientPage : public QWidget
{
    Q_OBJECT
public:
    explicit TcpClientPage(QWidget *parent = nullptr);
    ~TcpClientPage();

    bool connect(int port, const char* host = "127.0.0.1");
    void close();
    int send(const QString& msg);

protected:
    void initUI();
    void initConnect();
    virtual void customEvent(QEvent* e);

private:
    QLineEdit *hostEdt;
    QLineEdit *portEdt;
    QPushButton *connectBtn;
    QPushButton *closeBtn;

    hv::TcpClient *client;
};

#endif // TCP_CLIENT_PAGE_H
