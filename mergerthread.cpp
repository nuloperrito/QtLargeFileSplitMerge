#include "mergerthread.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>

MergerThread::MergerThread(QObject *parent)
    : QThread(parent), stopThread(false) {
}

void MergerThread::run() {
    QFile outputFileStream(outputFile);
    if (!outputFileStream.open(QIODevice::WriteOnly)) {
        emit error("Cannot open output file.");
        return;
    }

    stopThread = false;
    bool hasError = false;
    qint64 totalBytesWritten = 0;
    qint64 totalBytesToProcess = 0;

    // Pre-calculate total bytes of all input files for accurate progress tracking
    for (const QString& file : inputFiles) {
        totalBytesToProcess += QFileInfo(file).size();
    }
    
    // Allocate buffer once outside the loop using RAII
    const qint64 bufferSize = 4 * 1024 * 1024; // 4MB buffer
    QByteArray buffer;
    buffer.resize(bufferSize);
    char *bufData = buffer.data();
    
    int lastProgress = -1;

    for (int i = 0; i < inputFiles.size(); ++i) {
        if (stopThread || hasError) break;

        QFile inputFileStream(inputFiles.at(i));
        if (!inputFileStream.open(QIODevice::ReadOnly)) {
            emit error("Cannot open input file: " + inputFiles.at(i));
            hasError = true;
            break;
        }

        while (!inputFileStream.atEnd() && !stopThread) {
            qint64 bytesRead = inputFileStream.read(bufData, bufferSize);
            
            // Handle read error
            if (bytesRead < 0) {
                emit error("Error reading input file: " + inputFiles.at(i));
                hasError = true;
                break;
            }

            // EOF reached
            if (bytesRead == 0) {
                break;
            }

            // Handle write operation and verify bytes written
            qint64 bytesWritten = outputFileStream.write(bufData, bytesRead);
            if (bytesWritten != bytesRead) {
                emit error("Error writing to output file. Disk full?");
                hasError = true;
                break;
            }
            
            totalBytesWritten += bytesWritten;

            // Accurate progress calculation based on total bytes
            int prog = 0;
            if (totalBytesToProcess > 0) {
                prog = static_cast<int>((static_cast<double>(totalBytesWritten) / totalBytesToProcess) * 100);
            }
            if (prog != lastProgress) {
                emit progress(prog);
                lastProgress = prog;
            }
        }

        inputFileStream.close();
    }

    outputFileStream.close();

    // Cleanup output file if operation was aborted or errored
    if (stopThread || hasError) {
        QFile::remove(outputFile);
        if (stopThread) {
            emit aborted();
        }
    } else {
        emit processCompleted();
    }
}

void MergerThread::stop() {
    stopThread = true;
}
