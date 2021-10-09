#ifndef CUSTOM_EVENT_H
#define CUSTOM_EVENT_H

#include <QEvent>
#include <QString>

const static QEvent::Type qEventRecvMsg = (QEvent::Type)(QEvent::User + 1);

class QStringEvent : public QEvent {
public:
    QStringEvent(const QString& msg = "", QEvent::Type type = qEventRecvMsg) : QEvent(type)
    {
        message = msg;
    }
    QString message;
};

#endif // CUSTOM_EVENT_H
