#include "zdlogger.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QTextStream>

ZDLogger::ZDLogger()
{
}

void ZDLogger::open(QFile *file, const QString &path)
{
    if (file->isOpen())
        file->close();

    file->setFileName(path);
    file->open(QIODevice::WriteOnly | QIODevice::Append);
}

void ZDLogger::openLogFile(const QString &path)
{
    QMutexLocker locker(&m_mutex);

    open(&m_file, path);
}

void ZDLogger::openMCLogFile(const QString &path)
{
    QMutexLocker locker(&m_mcmutex);

    open(&m_mcfile, path);
}

void ZDLogger::writeLog(const QString &text)
{
    QMutexLocker locker(&m_mutex);

    if (m_file.isOpen()) {
        QTextStream ts(&m_file);
        ts.setCodec("UTF-8");
        ts << text << "\n";
    }
}

void ZDLogger::writeMCLog(const QString &text)
{
    QMutexLocker locker(&m_mcmutex);

    if (m_mcfile.isOpen()) {
        QTextStream ts(&m_mcfile);
        ts.setCodec("UTF-8");
        ts << text << "\n";
    }
}
