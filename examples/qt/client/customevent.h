#ifndef CUSTOM_EVENT_H
#define CUSTOM_EVENT_H

#include <QEvent>
#include <QString>

const static QEvent::Type qEventRecvMsg         = (QEvent::Type)(QEvent::User + 1);
const static QEvent::Type qEventConnected       = (QEvent::Type)(QEvent::User + 2);
const static QEvent::Type qEventDisconnected    = (QEvent::Type)(QEvent::User + 3);
const static QEvent::Type qEventOpened          = (QEvent::Type)(QEvent::User + 4);
const static QEvent::Type qEventClosed          = (QEvent::Type)(QEvent::User + 5);

class QStringEvent : public QEvent {
public:
    QStringEvent(const QString& msg = "", QEvent::Type type = qEventRecvMsg) : QEvent(type)
    {
        message = msg;
    }
    QString message;
};

#endif // CUSTOM_EVENT_H
