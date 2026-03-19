#include "splitterthread.h"
#include <QFile>
#include <QTextStream>

SplitterThread::SplitterThread(QObject *parent) :
    QThread(parent), stopThread(false) {
}

void SplitterThread::run() {
    QFile inputFileStream(inputFile);
    if (!inputFileStream.open(QIODevice::ReadOnly)) {
        emit error("Cannot open input file");
        return;
    }

    stopThread = false;
    bool finish = false;
    int count = 1;
    const qint64 bufferSize = 1024 * 1024; // 1MB buffer for reading
    char *buffer = new char[bufferSize]; // Dynamically allocate buffer

    while (!inputFileStream.atEnd() && !finish) {
        if (stopThread) break;

        // Create output filename with the required format
        QString outFileC = QString("%1/%2").arg(outputDir).arg(outputPattern.arg(count++)); // Modified: use outputPattern
        QFile outputFileStream(outFileC);
        if (!outputFileStream.open(QIODevice::WriteOnly)) {
            emit error("Cannot open output file");
            delete[] buffer; // Clean up buffer
            return;
        }

        qint64 totalBytesWritten = 0;

        while (totalBytesWritten < chunkSize) {
            if (stopThread) break;

            qint64 bytes = inputFileStream.read(buffer, bufferSize);

            if (bytes > 0) {
                outputFileStream.write(buffer, bytes);
                totalBytesWritten += bytes;
            }

            if (bytes == 0) {
                finish = true;
                break;
            }

            int prog = static_cast<int>((static_cast<double>(inputFileStream.pos()) / inputFileStream.size()) * 100);
            emit progress(prog);
        }

        outputFileStream.close();
    }

    delete[] buffer; // Clean up buffer
    inputFileStream.close();

    emit finished();
}

void SplitterThread::stop() {
    stopThread = true;
}
