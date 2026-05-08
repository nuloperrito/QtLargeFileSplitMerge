#ifndef MERGERTHREAD_H
#define MERGERTHREAD_H

#include <QThread>
#include <QStringList>
#include <QFile>
#include <QFileInfo>
#include <atomic>

class MergerThread : public QThread {
    Q_OBJECT

public:
    explicit MergerThread(QObject* parent = nullptr);
    void run() override;

    void setInputFiles(const QStringList& inputFiles) {
        this->inputFiles = inputFiles;
    }

    void setOutputFile(const QString& outputFile) {
        this->outputFile = outputFile;
    }

    void stop();

private:
    QStringList inputFiles;
    QString outputFile;
    std::atomic<bool> stopThread;

signals:
    void processCompleted();
    void progress(int value);
    void error(const QString &message);
    void aborted();

public slots:
};

#endif // MERGERTHREAD_H
