#ifndef SPLITTERTHREAD_H
#define SPLITTERTHREAD_H

#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <atomic>

class SplitterThread : public QThread {
    Q_OBJECT

public:
    explicit SplitterThread(QObject* parent = nullptr);
    void run() override;

    void setInputFile(const QString& inputFile) {
        this->inputFile = inputFile;
    }

    void setOutputDir(const QString& outputDir) {
        this->outputDir = outputDir;
    }

    void setOutPrefix(const QString& prefix) {
        this->outPrefix = prefix;
    }
    void setOutSuffix(const QString& suffix) {
        this->outSuffix = suffix;
    }

    void setChunkSize(qint64 chunkSize) {
        this->chunkSize = chunkSize;
    }

    void stop();

private:
    QString inputFile;
    QString outputDir;
    QString outPrefix;
    QString outSuffix;
    qint64 chunkSize;

    std::atomic<bool> stopThread;

signals:
    void processCompleted();
    void progress(int value);
    void error(const QString &message);
    void aborted();

public slots:

};

#endif // SPLITTERTHREAD_H
