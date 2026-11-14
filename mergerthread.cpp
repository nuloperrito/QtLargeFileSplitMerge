#include "mergerthread.h"
#include "diskiohelper.h"
#include <QByteArray>
#include <QCryptographicHash>

MergerThread::MergerThread(QObject *parent)
    : QThread(parent), stopThread(false)
{
}

void MergerThread::run()
{
    if (inputFiles.isEmpty()) {
        emit error("No input files provided for merging.");
        return;
    }

    QFile outputFileStream;
    if (!DiskIOHelper::openSequentialFile(outputFileStream, outputFile, QIODevice::WriteOnly)) {
        emit error("Cannot open output file.");
        return;
    }

    stopThread.store(false, std::memory_order_relaxed);
    bool hasError = false;
    qint64 totalBytesWritten = 0;
    qint64 lastFlushedOffset = 0;
    qint64 totalBytesToProcess = 0;

    for (const QString &file : inputFiles) {
        totalBytesToProcess += QFileInfo(file).size();
    }

    if (!DiskIOHelper::preallocateFile(outputFileStream, totalBytesToProcess)) {
        emit error("Failed to preallocate disk space for output file. Disk full?");
        outputFileStream.close();
        QFile::remove(outputFile);
        return;
    }

    QByteArray buffer;
    buffer.resize(DiskIOHelper::kDefaultBufferSize);
    char *bufData = buffer.data();

    QCryptographicHash sourceHash(DiskIOHelper::kHashAlgorithm);
    int lastProgress = -1;

    for (int i = 0; i < inputFiles.size(); ++i) {
        if (stopThread.load(std::memory_order_relaxed) || hasError) {
            break;
        }

        QFile inputFileStream;
        if (!DiskIOHelper::openSequentialFile(inputFileStream, inputFiles.at(i), QIODevice::ReadOnly)) {
            emit error("Cannot open input file: " + inputFiles.at(i));
            hasError = true;
            break;
        }

        while (!stopThread.load(std::memory_order_relaxed)) {
            const qint64 bytesRead = inputFileStream.read(bufData, DiskIOHelper::kDefaultBufferSize);

            if (bytesRead < 0) {
                emit error("Error reading input file: " + inputFiles.at(i));
                hasError = true;
                break;
            }
            if (bytesRead == 0) {
                break; // EOF
            }

            sourceHash.addData(bufData, static_cast<int>(bytesRead));

            if (!DiskIOHelper::writeAll(outputFileStream, bufData, bytesRead, stopThread)) {
                if (!stopThread.load(std::memory_order_relaxed)) {
                    emit error("Error writing to output file. Disk full or I/O failure?");
                    hasError = true;
                }
                break;
            }

            totalBytesWritten += bytesRead;

            if ((totalBytesWritten - lastFlushedOffset) >= DiskIOHelper::kFlushInterval) {
                if (!DiskIOHelper::flushAndEvictCache(outputFileStream, lastFlushedOffset, totalBytesWritten - lastFlushedOffset)) {
                    emit error("Hardware flush failed while writing output file.");
                    hasError = true;
                    break;
                }
                lastFlushedOffset = totalBytesWritten;
            }

            if (totalBytesToProcess > 0) {
                // Map write phase to 0% - 85%
                int prog = static_cast<int>((static_cast<double>(totalBytesWritten) / totalBytesToProcess) * 85);
                if (prog != lastProgress) {
                    emit progress(prog);
                    lastProgress = prog;
                }
            }
        }

        inputFileStream.close();
    }

    if (!hasError && !stopThread.load(std::memory_order_relaxed)) {
        if (!DiskIOHelper::flushAndEvictCache(outputFileStream, lastFlushedOffset, totalBytesWritten - lastFlushedOffset)) {
            emit error("Final disk flush failed for merged output file.");
            hasError = true;
        }
    }

    outputFileStream.close();

    // Physical read-back verification bypassing OS RAM page cache (85% - 100%)
    if (!stopThread.load(std::memory_order_relaxed) && !hasError) {
        QString verifyErr;
        bool verified = DiskIOHelper::verifyFilesOnDisk(
            QStringList{outputFile},
            sourceHash.result(),
            totalBytesToProcess,
            stopThread,
            [this, totalBytesToProcess, &lastProgress](qint64 verifiedBytes) {
                if (totalBytesToProcess > 0) {
                    int prog = 85 + static_cast<int>((static_cast<double>(verifiedBytes) / totalBytesToProcess) * 15);
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
        QFile::remove(outputFile);
        if (stopThread.load(std::memory_order_relaxed)) {
            emit aborted();
        }
    } else {
        emit progress(100);
        emit processCompleted();
    }
}

void MergerThread::stop()
{
    stopThread.store(true, std::memory_order_relaxed);
}