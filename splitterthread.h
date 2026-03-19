#ifndef SPLITTERTHREAD_H
#define SPLITTERTHREAD_H

#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QString>

class SplitterThread : public QThread {
    Q_OBJECT

public:
    explicit SplitterThread(QObject* parent = 0);
    void run() override;

    void setInputFile(const QString& inputFile) {
        this->inputFile = inputFile;
    }

    void setOutputDir(const QString& outputDir) {
        this->outputDir = outputDir;
    }

    void setOutputPattern(const QString& outputPattern) {
        this->outputPattern = outputPattern;
    }

    void setChunkSize(qint64 chunkSize) {
        this->chunkSize = chunkSize;
    }

    void stop();

private:
    QString inputFile;
    QString outputDir;
    QString outputPattern;
    qint64 chunkSize;

    bool stopThread;

signals:
    void finished();
    void progress(int value);
    void error(QString message);

public slots:

};

#endif // SPLITTERTHREAD_H
