#ifndef DISKIOHELPER_H
#define DISKIOHELPER_H

#include <QFile>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QCryptographicHash>
#include <atomic>
#include <functional>

class DiskIOHelper
{
public:
    static constexpr qint64 kDefaultBufferSize = 4 * 1024 * 1024;   // 4 MB (4096-byte sector aligned)
    static constexpr qint64 kFlushInterval     = 64 * 1024 * 1024;  // Force hardware sync every 64 MB
    static constexpr size_t kSectorAlignment   = 4096;              // 4Kn / 512e physical sector alignment
    static constexpr QCryptographicHash::Algorithm kHashAlgorithm = QCryptographicHash::Sha256;

    // Open file with OS-level sequential access hints
    static bool openSequentialFile(QFile &file, const QString &path, QIODevice::OpenMode mode);

    // Preallocate contiguous disk blocks to prevent filesystem fragmentation and head thrashing
    static bool preallocateFile(QFile &file, qint64 expectedSize);

    // Guaranteed full write loop handling partial writes and cancellation
    static bool writeAll(QFile &file, const char *data, qint64 size, const std::atomic<bool> &stopFlag);

    // Flush userspace buffer + OS dirty pages to physical disk platters and evict clean cache pages
    static bool flushAndEvictCache(QFile &file, qint64 offset, qint64 length);

    // Read back written files bypassing OS RAM page cache and verify SHA-256 hash
    static bool verifyFilesOnDisk(const QStringList &files,
                                  const QByteArray &expectedHash,
                                  qint64 totalBytes,
                                  const std::atomic<bool> &stopFlag,
                                  const std::function<void(qint64)> &onBytesVerified,
                                  QString &errorOut);

private:
    static void* allocateAlignedBuffer(size_t size, size_t alignment);
    static void freeAlignedBuffer(void *ptr);
};

#endif // DISKIOHELPER_H