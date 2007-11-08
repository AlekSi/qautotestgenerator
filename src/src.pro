TEMPLATE = app
TARGET = qautotestgenerator
DEPENDPATH += . 
INCLUDEPATH += .

unix:CONFIG += debug_and_release
mac:CONFIG -= app_bundle
win32: CONFIG += console

RESOURCES += generator.qrc

include(../rpp/src/rxx.pri)
include(../rpp/src/rpp/rpp.pri)

SOURCES += main.cpp
DESTDIR = ../

QT = core
