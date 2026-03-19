#include "mergerthread.h"
#include <QFile>
#include <QTextStream>

MergerThread::MergerThread(QObject *parent)
    : QThread(parent), stopThread(false) {
}

void MergerThread::run() {
    QFile outputFileStream(outputFile);
    if (!outputFileStream.open(QIODevice::WriteOnly)) {
        emit error("Cannot open output file");
        return;
    }

    stopThread = false;
    qint64 totalBytesWritten = 0;

    for (int i = 0; i < inputFiles.size(); ++i) {
        if (stopThread) break;

        QFile inputFileStream(inputFiles.at(i));
        if (!inputFileStream.open(QIODevice::ReadOnly)) {
            emit error("Cannot open input file: " + inputFiles.at(i));
            return;
        }

        const qint64 bufferSize = 1024 * 1024; // 1MB buffer for reading
        char *buffer = new char[bufferSize]; // Dynamically allocate buffer

        while (!inputFileStream.atEnd() && !stopThread) {
            qint64 bytes = inputFileStream.read(buffer, bufferSize);
            if (bytes > 0) {
                outputFileStream.write(buffer, bytes);
                totalBytesWritten += bytes;
            }

            int prog = static_cast<int>((static_cast<double>(i) / inputFiles.size() + (static_cast<double>(inputFileStream.pos()) / inputFileStream.size()) / inputFiles.size()) * 100);
            emit progress(prog);
        }

        delete[] buffer; // Clean up buffer
        inputFileStream.close();
    }

    outputFileStream.close();

    emit finished();
}

void MergerThread::stop() {
    stopThread = true;
}
