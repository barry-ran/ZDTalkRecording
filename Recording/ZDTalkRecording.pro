QT += core network winextras
QT -= gui

CONFIG += c++11

TARGET = ZDTalkRecording
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app
Debug: DESTDIR += ../../bin/debug-teacher-rec
Release: DESTDIR += ../../bin/release-teacher-rec

# OBS
INCLUDEPATH += $$PWD/../../ThirdParty/obs-studio/libobs
INCLUDEPATH += $$PWD/../../ThirdParty/obs-studio/dependencies2015/win32/include

Debug:LIBS   += $$PWD/../../ThirdParty/obs-studio/build/debug/lib/obs.lib
Release:LIBS += $$PWD/../../ThirdParty/obs-studio/build/release/lib/obs.lib

# CrashRpt
INCLUDEPATH  += $$PWD/../../ThirdParty/CrashRpt/include
Debug:LIBS   += $$PWD/../../ThirdParty/CrashRpt/lib/CrashRpt1402d.lib
Release:LIBS += $$PWD/../../ThirdParty/CrashRpt/lib/CrashRpt1402.lib

LIBS += -lDbgHelp -lAdvapi32 -lOle32

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_MESSAGELOGCONTEXT

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

INCLUDEPATH += $$PWD/../

HEADERS += \
    $$PWD/../utils/log/zdlogger.h \
    $$PWD/../platform.h \
    $$PWD/../utils/singlebase/singlebase.h \
    zdobscontext.h \
    zdrecordingdefine.h \
    zdrecordingversion.h \
    zdrecordingclient.h

SOURCES += \
    $$PWD/../utils/log/zdlogger.cpp \
    $$PWD/../crashhandler.cpp \
    $$PWD/../platform.cpp \
    $$PWD/../utils/singlebase/singlebase.cpp \
    main.cpp \
    zdobscontext.cpp \
    zdrecordingclient.cpp

RC_FILE = app.rc
