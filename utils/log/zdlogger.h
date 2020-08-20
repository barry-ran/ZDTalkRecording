#ifndef ZDLOGGER_H
#define ZDLOGGER_H

#include "utils/singlebase/singlebase.h"
#include <QFile>

class ZDLogger : public Singleton<ZDLogger>
{
public:
    friend class Singleton<ZDLogger>;
    ~ZDLogger() {}

    void writeLog(const QString &);
    void writeMCLog(const QString &);

    QString logPath() { return m_file.fileName(); }
    QString mcLogPath() { return m_mcfile.fileName(); }

    void openLogFile(const QString &);
    void openMCLogFile(const QString &);

private:
    void open(QFile *, const QString &);

private:
    ZDLogger();

    QFile m_file;
    QMutex m_mutex;

    QFile m_mcfile;
    QMutex m_mcmutex;
};

#endif // ZDLOGGER_H
