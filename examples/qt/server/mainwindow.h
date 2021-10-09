#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>

#include <QTabWidget>
#include <QListWidget>

#include "hv/singleton.h"

class MainWindow : public QMainWindow
{
    SINGLETON_DECL(MainWindow)
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void initUI();
    void initMenu();
    void initConnect();

    void postMessage(const QString& msg);
    void appendMessage(const QString& msg);
    void showMessage(const QString& msg);

protected:
    virtual void customEvent(QEvent* e);

private:
    QWidget *center;
    QTabWidget *tab;
    QTextEdit *recvmsg;
};

#define g_mainwnd MainWindow::instance()

#endif // MAINWINDOW_H
