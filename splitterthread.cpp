#include "splitterthread.h"
#include <QFile>
#include <QDir>
#include <QByteArray>

SplitterThread::SplitterThread(QObject *parent) :
    QThread(parent), stopThread(false) {
}

void SplitterThread::run() {
    QFile inputFileStream(inputFile);
    if (!inputFileStream.open(QIODevice::ReadOnly)) {
        emit error("Cannot open input file.");
        return;
    }

    stopThread = false;
    bool finish = false;
    bool hasError = false;
    int count = 1;
    
    // Increase buffer size to 4MB for better I/O throughput
    const qint64 bufferSize = 4 * 1024 * 1024; 
    QByteArray buffer;
    buffer.resize(bufferSize);
    char *bufData = buffer.data();
    
    int lastProgress = -1;

    QStringList generatedFiles; // Track created files for rollback

    while (!inputFileStream.atEnd() && !finish && !stopThread && !hasError) {
        // Create output filename with the required format
        QString currentFileName = outPrefix + "_" + QString::number(count++);
        if (!outSuffix.isEmpty()) {
            currentFileName += "." + outSuffix;
        }
        QString outFileC = QDir(outputDir).filePath(currentFileName);
        generatedFiles.append(outFileC);
        QFile outputFileStream(outFileC);
        
        if (QFileInfo(outFileC).absoluteFilePath() == QFileInfo(inputFile).absoluteFilePath()) {
            emit error("Error: Generated output file path collides with the input file!");
            hasError = true;
            break;
        }
        if (!outputFileStream.open(QIODevice::WriteOnly)) {
            emit error("Cannot open output file: " + outFileC);
            hasError = true;
            break;
        }

        qint64 totalBytesWritten = 0;

        while (totalBytesWritten < chunkSize) {
            if (stopThread) break;

            // Prevent reading beyond the requested chunk size
            qint64 bytesToRead = qMin((qint64)bufferSize, chunkSize - totalBytesWritten);
            qint64 bytesRead = inputFileStream.read(bufData, bytesToRead);

            // Handle read error
            if (bytesRead < 0) {
                emit error("Error reading input file.");
                hasError = true;
                break;
            }

            // EOF reached
            if (bytesRead == 0) {
                finish = true;
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

            // Throttle progress signals to prevent UI thread event congestion
            int prog = 0;
            if (inputFileStream.size() > 0) {
                prog = static_cast<int>((static_cast<double>(inputFileStream.pos()) / inputFileStream.size()) * 100);
            }
            if (prog != lastProgress) {
                emit progress(prog);
                lastProgress = prog;
            }
        }

        outputFileStream.close();
    }

    inputFileStream.close();
    
    if (stopThread || hasError) {
        for (const QString &file : generatedFiles) {
            QFile::remove(file);
        }
        if (stopThread) {
            emit aborted();
        }
    } else {
        emit processCompleted();
    }
}

void SplitterThread::stop() {
    stopThread = true;
}
