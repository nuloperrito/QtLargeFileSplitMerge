#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "diskiohelper.h"
#include <cstdlib>
#include <cerrno>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <malloc.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

void* DiskIOHelper::allocateAlignedBuffer(size_t size, size_t alignment)
{
#ifdef Q_OS_WIN
    return _aligned_malloc(size, alignment);
#else
    void *ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

void DiskIOHelper::freeAlignedBuffer(void *ptr)
{
    if (!ptr) {
        return;
    }
#ifdef Q_OS_WIN
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

bool DiskIOHelper::openSequentialFile(QFile &file, const QString &path, QIODevice::OpenMode mode)
{
#ifdef Q_OS_WIN
    const bool isRead = (mode == QIODevice::ReadOnly);
    DWORD access   = isRead ? GENERIC_READ : GENERIC_WRITE;
    DWORD share    = isRead ? FILE_SHARE_READ : 0;
    DWORD creation = isRead ? OPEN_EXISTING : CREATE_ALWAYS;
    int crtFlags   = isRead ? (_O_RDONLY | _O_BINARY) : (_O_WRONLY | _O_BINARY);

    HANDLE hFile = CreateFileW(
        reinterpret_cast<const wchar_t*>(path.utf16()),
        access,
        share,
        nullptr,
        creation,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), crtFlags);
    if (fd == -1) {
        CloseHandle(hFile);
        return false;
    }

    if (!file.open(fd, mode, QFileDevice::AutoCloseHandle)) {
        _close(fd);
        return false;
    }
    return true;
#else
    const bool isRead = (mode == QIODevice::ReadOnly);
    int flags = isRead ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif

    const QByteArray nativePath = QFile::encodeName(path);
    int fd = ::open(nativePath.constData(), flags, 0666);
    if (fd == -1) {
        return false;
    }

#if defined(Q_OS_LINUX)
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#elif defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
    if (isRead) {
        ::fcntl(fd, F_RDAHEAD, 1);
    }
#endif

    if (!file.open(fd, mode, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
    return true;
#endif
}

bool DiskIOHelper::preallocateFile(QFile &file, qint64 expectedSize)
{
    if (expectedSize <= 0) {
        return true;
    }

    const int fd = file.handle();
    if (fd == -1) {
        return file.resize(expectedSize);
    }

#ifdef Q_OS_WIN
    HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER liSize;
    liSize.QuadPart = expectedSize;
    if (!SetFilePointerEx(hFile, liSize, nullptr, FILE_BEGIN)) {
        return false;
    }
    if (!SetEndOfFile(hFile)) {
        return false;
    }
    LARGE_INTEGER liZero;
    liZero.QuadPart = 0;
    return SetFilePointerEx(hFile, liZero, nullptr, FILE_BEGIN) != 0;

#elif defined(Q_OS_LINUX)
    int res = ::fallocate(fd, 0, 0, static_cast<off_t>(expectedSize));
    if (res != 0) {
        // Fallback to ftruncate on filesystems without fallocate support (e.g., exFAT, NTFS-3G, SMB)
        if (errno == EOPNOTSUPP || errno == ENOSYS) {
            if (::ftruncate(fd, static_cast<off_t>(expectedSize)) != 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    return ::lseek(fd, 0, SEEK_SET) == 0;

#elif defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
    fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, expectedSize, 0};
    if (::fcntl(fd, F_PREALLOCATE, &store) == -1) {
        store.fst_flags = F_ALLOCATEALL;
        ::fcntl(fd, F_PREALLOCATE, &store); // Best-effort allocation; proceed to ftruncate
    }
    if (::ftruncate(fd, static_cast<off_t>(expectedSize)) != 0) {
        return false;
    }
    return ::lseek(fd, 0, SEEK_SET) == 0;

#else
    if (::ftruncate(fd, static_cast<off_t>(expectedSize)) != 0) {
        return false;
    }
    return ::lseek(fd, 0, SEEK_SET) == 0;
#endif
}

bool DiskIOHelper::writeAll(QFile &file, const char *data, qint64 size, const std::atomic<bool> &stopFlag)
{
    qint64 totalWritten = 0;
    while (totalWritten < size) {
        if (stopFlag.load(std::memory_order_relaxed)) {
            return false;
        }
        qint64 written = file.write(data + totalWritten, size - totalWritten);
        if (written <= 0) {
            return false;
        }
        totalWritten += written;
    }
    return true;
}

bool DiskIOHelper::flushAndEvictCache(QFile &file, qint64 offset, qint64 length)
{
    if (!file.flush()) {
        return false;
    }

    const int fd = file.handle();
    if (fd != -1) {
#ifdef Q_OS_WIN
        HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        if (hFile != INVALID_HANDLE_VALUE) {
            if (!FlushFileBuffers(hFile)) {
                return false;
            }
        }
#elif defined(Q_OS_LINUX)
        if (::fdatasync(fd) != 0) {
            return false;
        }
        // Immediately drop synced pages from Linux Page Cache to prevent 30GB+ RAM cache buildup
        if (length > 0) {
            ::posix_fadvise(fd, static_cast<off_t>(offset), static_cast<off_t>(length), POSIX_FADV_DONTNEED);
        }
#elif defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
        // F_FULLFSYNC forces the drive controller to flush its volatile cache to physical platters
        if (::fcntl(fd, F_FULLFSYNC) == -1) {
            if (::fsync(fd) != 0) {
                return false;
            }
        }
#else
        if (::fsync(fd) != 0) {
            return false;
        }
#endif
    }

    return file.error() == QFileDevice::NoError;
}

bool DiskIOHelper::verifyFilesOnDisk(const QStringList &files,
                                     const QByteArray &expectedHash,
                                     qint64 totalBytes,
                                     const std::atomic<bool> &stopFlag,
                                     const std::function<void(qint64)> &onBytesVerified,
                                     QString &errorOut)
{
    Q_UNUSED(totalBytes);

    char *alignedBuf = static_cast<char*>(allocateAlignedBuffer(static_cast<size_t>(kDefaultBufferSize), kSectorAlignment));
    if (!alignedBuf) {
        errorOut = "Failed to allocate sector-aligned buffer for physical disk verification.";
        return false;
    }

    QCryptographicHash verifyHash(kHashAlgorithm);
    qint64 verifiedBytes = 0;
    bool ok = true;

    for (const QString &filePath : files) {
        if (stopFlag.load(std::memory_order_relaxed)) {
            ok = false;
            break;
        }

#ifdef Q_OS_WIN
        HANDLE hFile = CreateFileW(
            reinterpret_cast<const wchar_t*>(filePath.utf16()),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );

        // Fallback if filesystem (e.g., network share or virtual drive) rejects FILE_FLAG_NO_BUFFERING
        if (hFile == INVALID_HANDLE_VALUE) {
            hFile = CreateFileW(
                reinterpret_cast<const wchar_t*>(filePath.utf16()),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_SEQUENTIAL_SCAN,
                nullptr
            );
        }

        if (hFile == INVALID_HANDLE_VALUE) {
            errorOut = "Verification failed: Cannot reopen file " + filePath;
            ok = false;
            break;
        }

        while (!stopFlag.load(std::memory_order_relaxed)) {
            DWORD bytesRead = 0;
            BOOL readRes = ReadFile(hFile, alignedBuf, static_cast<DWORD>(kDefaultBufferSize), &bytesRead, nullptr);
            if (!readRes) {
                errorOut = "Physical disk read error during verification: " + filePath;
                ok = false;
                break;
            }
            if (bytesRead == 0) {
                break; // EOF
            }
            verifyHash.addData(alignedBuf, static_cast<int>(bytesRead));
            verifiedBytes += bytesRead;
            if (onBytesVerified) {
                onBytesVerified(verifiedBytes);
            }
        }
        CloseHandle(hFile);
        if (!ok) {
            break;
        }
#else
        const QByteArray nativePath = QFile::encodeName(filePath);
        int baseFlags = O_RDONLY;
#ifdef O_CLOEXEC
        baseFlags |= O_CLOEXEC;
#endif

        int fd = -1;
        bool usingDirectIO = false;

#if defined(Q_OS_LINUX) && defined(O_DIRECT)
        fd = ::open(nativePath.constData(), baseFlags | O_DIRECT);
        if (fd != -1) {
            usingDirectIO = true;
        } else {
            // Fallback for filesystems without O_DIRECT support (e.g., tmpfs, ZFS, exFAT, NTFS-3G)
            fd = ::open(nativePath.constData(), baseFlags);
            if (fd != -1) {
                ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
                ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            }
        }
#else
        fd = ::open(nativePath.constData(), baseFlags);
#if defined(Q_OS_MACOS) || defined(Q_OS_DARWIN)
        if (fd != -1) {
            // Bypass macOS Unified Buffer Cache to force physical disk reads
            ::fcntl(fd, F_NOCACHE, 1);
            ::fcntl(fd, F_RDAHEAD, 1);
        }
#elif defined(Q_OS_LINUX)
        if (fd != -1) {
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        }
#endif
#endif

        if (fd == -1) {
            errorOut = "Verification failed: Cannot reopen file " + filePath;
            ok = false;
            break;
        }

        off_t fileOffset = 0;
        while (!stopFlag.load(std::memory_order_relaxed)) {
            ssize_t bytesRead = ::read(fd, alignedBuf, static_cast<size_t>(kDefaultBufferSize));
            if (bytesRead < 0) {
                if (errno == EINTR) {
                    continue;
                }
#if defined(Q_OS_LINUX)
                // On strict Linux filesystems, O_DIRECT may return EINVAL on non-sector-aligned EOF tails.
                // Seamlessly reopen without O_DIRECT at the exact offset to read the remaining tail bytes.
                if (usingDirectIO && errno == EINVAL) {
                    ::close(fd);
                    fd = ::open(nativePath.constData(), baseFlags);
                    if (fd == -1 || ::lseek(fd, fileOffset, SEEK_SET) != fileOffset) {
                        errorOut = "Failed to reopen unaligned file tail during verification: " + filePath;
                        ok = false;
                        break;
                    }
                    ::posix_fadvise(fd, fileOffset, 0, POSIX_FADV_DONTNEED);
                    usingDirectIO = false;
                    continue;
                }
#endif
                errorOut = "Physical disk read error during verification: " + filePath;
                ok = false;
                break;
            }
            if (bytesRead == 0) {
                break; // EOF
            }

            verifyHash.addData(alignedBuf, static_cast<int>(bytesRead));
            fileOffset += bytesRead;
            verifiedBytes += bytesRead;
            if (onBytesVerified) {
                onBytesVerified(verifiedBytes);
            }
        }

        if (fd != -1) {
#if defined(Q_OS_LINUX)
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
            ::close(fd);
        }

        if (!ok) {
            break;
        }
#endif
    }

    freeAlignedBuffer(alignedBuf);

    if (!ok || stopFlag.load(std::memory_order_relaxed)) {
        return false;
    }

    if (verifyHash.result() != expectedHash) {
        errorOut = "Data corruption detected on target disk! Written data hash does not match source.";
        return false;
    }

    return true;
}