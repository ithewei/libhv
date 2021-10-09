#ifndef HTTP_CLIENT_PAGE_H
#define HTTP_CLIENT_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>

#include "hv/requests.h"

class HttpClientPage : public QWidget
{
    Q_OBJECT
public:
    explicit HttpClientPage(QWidget *parent = nullptr);
    ~HttpClientPage();

    int send(const QString& msg);

protected:
    void initUI();
    void initConnect();

private:
    QComboBox *method;
    QLineEdit *urlEdt;
};

#endif // HTTP_CLIENT_PAGE_H
