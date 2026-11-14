#include "splitterthread.h"
#include "diskiohelper.h"
#include <QDir>
#include <QByteArray>
#include <QCryptographicHash>

SplitterThread::SplitterThread(QObject *parent)
    : QThread(parent), chunkSize(0), stopThread(false)
{
}

void SplitterThread::run()
{
    if (chunkSize <= 0) {
        emit error("Invalid chunk size.");
        return;
    }

    QFile inputFileStream;
    if (!DiskIOHelper::openSequentialFile(inputFileStream, inputFile, QIODevice::ReadOnly)) {
        emit error("Cannot open input file.");
        return;
    }

    stopThread.store(false, std::memory_order_relaxed);
    bool hasError = false;
    int count = 1;

    const qint64 totalInputSize = inputFileStream.size();
    qint64 totalInputBytesRead = 0;

    QByteArray buffer;
    buffer.resize(DiskIOHelper::kDefaultBufferSize);
    char *bufData = buffer.data();

    QCryptographicHash sourceHash(DiskIOHelper::kHashAlgorithm);
    int lastProgress = -1;
    QStringList generatedFiles;

    // Handle 0-byte input file explicitly; otherwise loop until exact totalInputSize is processed
    bool firstIteration = true;
    while ((totalInputBytesRead < totalInputSize || (totalInputSize == 0 && firstIteration))
           && !stopThread.load(std::memory_order_relaxed)
           && !hasError) {
        firstIteration = false;

        QString currentFileName = outPrefix + "_" + QString::number(count++);
        if (!outSuffix.isEmpty()) {
            currentFileName += "." + outSuffix;
        }
        QString outFileC = QDir(outputDir).filePath(currentFileName);

        if (QFileInfo(outFileC).absoluteFilePath() == QFileInfo(inputFile).absoluteFilePath()) {
            emit error("Error: Generated output file path collides with the input file!");
            hasError = true;
            break;
        }

        generatedFiles.append(outFileC);

        QFile outputFileStream;
        if (!DiskIOHelper::openSequentialFile(outputFileStream, outFileC, QIODevice::WriteOnly)) {
            emit error("Cannot open output file: " + outFileC);
            hasError = true;
            break;
        }

        const qint64 remainingBytes = totalInputSize - totalInputBytesRead;
        const qint64 expectedChunkSize = qMin(chunkSize, remainingBytes);
        if (!DiskIOHelper::preallocateFile(outputFileStream, expectedChunkSize)) {
            emit error("Failed to preallocate disk space for: " + outFileC + ". Disk full?");
            outputFileStream.close();
            hasError = true;
            break;
        }

        qint64 chunkBytesWritten = 0;
        qint64 lastFlushedOffset = 0;

        while (chunkBytesWritten < expectedChunkSize && !stopThread.load(std::memory_order_relaxed)) {
            const qint64 bytesToRead = qMin(DiskIOHelper::kDefaultBufferSize, expectedChunkSize - chunkBytesWritten);
            const qint64 bytesRead = inputFileStream.read(bufData, bytesToRead);

            if (bytesRead < 0) {
                emit error("Error reading input file.");
                hasError = true;
                break;
            }
            if (bytesRead == 0) {
                emit error("Unexpected EOF while reading input file.");
                hasError = true;
                break;
            }

            sourceHash.addData(bufData, static_cast<int>(bytesRead));

            if (!DiskIOHelper::writeAll(outputFileStream, bufData, bytesRead, stopThread)) {
                if (!stopThread.load(std::memory_order_relaxed)) {
                    emit error("Error writing to output file. Disk full or I/O failure?");
                    hasError = true;
                }
                break;
            }

            chunkBytesWritten += bytesRead;
            totalInputBytesRead += bytesRead;

            if ((chunkBytesWritten - lastFlushedOffset) >= DiskIOHelper::kFlushInterval) {
                if (!DiskIOHelper::flushAndEvictCache(outputFileStream, lastFlushedOffset, chunkBytesWritten - lastFlushedOffset)) {
                    emit error("Hardware flush failed while writing: " + outFileC);
                    hasError = true;
                    break;
                }
                lastFlushedOffset = chunkBytesWritten;
            }

            if (totalInputSize > 0) {
                // Map write phase to 0% - 85%
                int prog = static_cast<int>((static_cast<double>(totalInputBytesRead) / totalInputSize) * 85);
                if (prog != lastProgress) {
                    emit progress(prog);
                    lastProgress = prog;
                }
            }
        }

        if (!hasError && !stopThread.load(std::memory_order_relaxed)) {
            if (!DiskIOHelper::flushAndEvictCache(outputFileStream, lastFlushedOffset, chunkBytesWritten - lastFlushedOffset)) {
                emit error("Final disk flush failed for chunk: " + outFileC);
                hasError = true;
            }
        }

        outputFileStream.close();
    }

    inputFileStream.close();

    // Physical read-back verification bypassing OS RAM page cache (85% - 100%)
    if (!stopThread.load(std::memory_order_relaxed) && !hasError) {
        QString verifyErr;
        bool verified = DiskIOHelper::verifyFilesOnDisk(
            generatedFiles,
            sourceHash.result(),
            totalInputSize,
            stopThread,
            [this, totalInputSize, &lastProgress](qint64 verifiedBytes) {
                if (totalInputSize > 0) {
                    int prog = 85 + static_cast<int>((static_cast<double>(verifiedBytes) / totalInputSize) * 15);
                    if (prog != lastProgress) {
                        emit progress(prog);
                        lastProgress = prog;
                    }
                }
            },
            verifyErr
        );

        if (!verified && !stopThread.load(std::memory_order_relaxed)) {
            emit error(verifyErr);
            hasError = true;
        }
    }

    if (stopThread.load(std::memory_order_relaxed) || hasError) {
        for (const QString &file : generatedFiles) {
            QFile::remove(file);
        }
        if (stopThread.load(std::memory_order_relaxed)) {
            emit aborted();
        }
    } else {
        emit progress(100);
        emit processCompleted();
    }
}

void SplitterThread::stop()
{
    stopThread.store(true, std::memory_order_relaxed);
}