QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

MOC_DIR     = tmp/moc
OBJECTS_DIR = tmp/obj

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    TcpServerPage.cpp \
    UdpServerPage.cpp \
    HttpServerPage.cpp

HEADERS += \
    customevent.h \
    mainwindow.h \
    TcpServerPage.h \
    UdpServerPage.h \
    HttpServerPage.h

win32 {
    # INCLUDEPATH += C:\libhv\include
    # LIBS += -LC:\libhv\lib -lhv

    INCLUDEPATH += ../../../build/mingw64/include
    LIBS += -L../../../build/mingw64/lib -lhv
    LIBS += -lws2_32
}

unix {
    # INCLUDEPATH += /usr/local/include
    # LIBS += -L/usr/local/lib -lhv

    INCLUDEPATH += ../../../include
    LIBS += -L../../../lib -lhv
}
