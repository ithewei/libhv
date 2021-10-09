#include "HttpClientPage.h"

#include <QBoxLayout>

#include "mainwindow.h"

HttpClientPage::HttpClientPage(QWidget *parent) : QWidget(parent)
{
    initUI();
    initConnect();
}

HttpClientPage::~HttpClientPage()
{
}

void HttpClientPage::initUI()
{
    QHBoxLayout* hbox = new QHBoxLayout;

    // method
    hbox->addWidget(new QLabel("method:"));
    method = new QComboBox;
    method->addItems({ "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD" });
    method->setCurrentText("POST");
    hbox->addWidget(method);

    // url
    hbox->addWidget(new QLabel("url:"));
    urlEdt = new QLineEdit("http://127.0.0.1:8080/echo");
    hbox->addWidget(urlEdt);

    setLayout(hbox);
}

void HttpClientPage::initConnect()
{
}

int HttpClientPage::send(const QString& msg)
{
    requests::Request req(new HttpRequest);
    req->SetMethod(method->currentText().toStdString().c_str());
    req->SetUrl(urlEdt->text().toStdString().c_str());
    req->SetBody(msg.toStdString());
    return requests::async(req, [](const requests::Response& resp) {
        if (resp == nullptr) {
            g_mainwnd->postMessage("request failed!");
        } else {
            g_mainwnd->postMessage(QString("received http response:\n") + QString::fromStdString(resp->Dump(true, true)));
        }
    });
}
