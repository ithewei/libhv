#ifndef WEB_SOCKET_CLIENT_PAGE_H
#define WEB_SOCKET_CLIENT_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include "hv/WebSocketClient.h"

class WebSocketClientPage : public QWidget
{
    Q_OBJECT
public:
    explicit WebSocketClientPage(QWidget *parent = nullptr);
    ~WebSocketClientPage();

    bool open(const char* url);
    void close();
    int send(const QString& msg);

protected:
    void initUI();
    void initConnect();
    virtual void customEvent(QEvent* e);

private:
    QLineEdit *urlEdt;
    QPushButton *openBtn;
    QPushButton *closeBtn;

    hv::WebSocketClient *client;
};

#endif // WEB_SOCKET_CLIENT_PAGE_H
