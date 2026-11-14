#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QShortcut>
#include <QCollator>
#include <QMessageBox>
#include <QIntValidator>
#include <limits>
#include <vector>
#include <algorithm>
#include <utility>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
                                          ui(new Ui::MainWindow),
                                          splitterThread(nullptr),
                                          mergerThread(nullptr),
                                          mergeFilesModel(new QStringListModel(this))
{
    ui->setupUi(this);

    setAcceptDrops(true);

    ui->listViewMergePath->setModel(mergeFilesModel);
    ui->listViewMergePath->setSelectionMode(QAbstractItemView::MultiSelection);

    connect(ui->bRunSplit, &QPushButton::clicked, this, &MainWindow::bRunSplitClick);
    connect(ui->bRunMerge, &QPushButton::clicked, this, &MainWindow::bRunMergeClick);
    connect(ui->bBrowseIn, &QPushButton::clicked, this, &MainWindow::bBrowseInClick);
    connect(ui->bBrowseOut, &QPushButton::clicked, this, &MainWindow::bBrowseOutClick);
    connect(ui->bAddMergeIn, &QPushButton::clicked, this, &MainWindow::bAddMergeInClick);
    connect(ui->bRemoveMergeIn, &QPushButton::clicked, this, &MainWindow::bRemoveMergeInClick);
    connect(ui->bBrowseMergeOut, &QPushButton::clicked, this, &MainWindow::bBrowseMergeOutClick);

    connect(new QShortcut(QKeySequence("F5"), this), &QShortcut::activated, this, &MainWindow::bRunSplitClick);
    connect(new QShortcut(QKeySequence("F2"), this), &QShortcut::activated, this, &MainWindow::bBrowseInClick);
    connect(new QShortcut(QKeySequence("F3"), this), &QShortcut::activated, this, &MainWindow::bBrowseOutClick);
    connect(new QShortcut(QKeySequence("Esc"), this), &QShortcut::activated, this, &MainWindow::close);

    setProgressBarState(0, false, false);
    setWindowTitle("File Splitter & Merger");
    setFixedSize(this->width(), this->height());
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    ui->cbUnit->setCurrentIndex(1);
    ui->leChunkSize->setValidator(new QIntValidator(1, 999999999, this));
}

MainWindow::~MainWindow()
{
    if (splitterThread)
    {
        splitterThread->stop();
        splitterThread->wait();
    }
    if (mergerThread)
    {
        mergerThread->stop();
        mergerThread->wait();
    }
    delete ui;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls())
    {
        return;
    }

    QList<QUrl> urls = mimeData->urls();
    int currentTab = ui->tabWidget->currentIndex();

    if (currentTab == 0)
    {
        // Split Tab Logic
        if (urls.size() != 1)
        {
            return; // Only accept single file or directory
        }

        QFileInfo fileInfo(urls.first().toLocalFile());
        if (fileInfo.isFile())
        {
            ui->leSplitPath->setText(fileInfo.absoluteFilePath());
        }
        else if (fileInfo.isDir())
        {
            ui->leSplitOutDir->setText(fileInfo.absoluteFilePath());
        }
    }
    else if (currentTab == 1)
    {
        // Merge Tab Logic
        QStringList newFiles;
        for (const QUrl &url : urls)
        {
            QFileInfo fileInfo(url.toLocalFile());
            if (fileInfo.isFile())
            { // Exclude directories
                newFiles.append(fileInfo.absoluteFilePath());
            }
        }

        addFilesToMergeList(newFiles);
    }

    event->acceptProposedAction();
}

void MainWindow::handleError(const QString &message)
{
    QMessageBox::critical(this, "Error", message);
    disableOperations(false);
    setProgressBarState(0, false, true);
    status("Operation failed with error");
}

void MainWindow::addFilesToMergeList(const QStringList &newFiles)
{
    if (newFiles.isEmpty())
        return;

    QStringList currentFiles = mergeFilesModel->stringList();
    currentFiles.append(newFiles);

    // Remove duplicates to prevent the same file from being merged multiple times
    QStringList uniqueFiles;
    for (const QString &path : currentFiles)
    {
        QFileInfo fi(path);
        if (fi.exists())
        {
            QString canonical = fi.canonicalFilePath();
            if (!uniqueFiles.contains(canonical))
            {
                uniqueFiles.append(canonical);
            }
        }
    }
    currentFiles = uniqueFiles;

    // Apply natural sorting to the entire combined list
    std::vector<std::pair<QString, QString>> filePairs;
    filePairs.reserve(currentFiles.size());
    for (const QString &path : currentFiles)
    {
        filePairs.emplace_back(path, QFileInfo(path).fileName());
    }

    QCollator collator;
    collator.setNumericMode(true);

    std::sort(filePairs.begin(), filePairs.end(), [&collator](const std::pair<QString, QString> &a, const std::pair<QString, QString> &b)
              { return collator.compare(a.second, b.second) < 0; });

    currentFiles.clear();
    for (const auto &pair : filePairs)
    {
        currentFiles.append(pair.first);
    }

    mergeFilesModel->setStringList(currentFiles);
}

void MainWindow::bRunSplitClick()
{
    // Safely clean up existing thread before creating a new one
    if (splitterThread)
    {
        if (splitterThread->isRunning())
        {
            splitterThread->stop();
            ui->bRunSplit->setText("Stopping...");
            ui->bRunSplit->setDisabled(true);
            status("Stopping and cleaning up...");
            return;
        }
    }

    QString inputFile = ui->leSplitPath->text();
    QString outDir = ui->leSplitOutDir->text();

    if (inputFile.trimmed().isEmpty() || !QFileInfo::exists(inputFile))
    {
        status("Please provide a valid input file!");
        return;
    }
    if (outDir.trimmed().isEmpty())
    {
        status("Please provide a valid output directory!");
        return;
    }

    qint64 chunkSize = ui->leChunkSize->text().toLongLong();

    if (chunkSize <= 0)
    {
        status("Chunk size must be greater than 0!");
        return;
    }

    int unit_idx = ui->cbUnit->currentIndex();
    // unit_idx 0 = KB (<< 10), 1 = MB (<< 20), 2 = GB (<< 30)
    int shiftBits = 10 * (unit_idx + 1);
    qint64 maxAllowed = std::numeric_limits<qint64>::max() >> shiftBits;
    if (chunkSize > maxAllowed)
    {
        status("Chunk size is too large (Overflow)!");
        return;
    }

    chunkSize <<= shiftBits;

    status("");

    outDir = formatPath(outDir);
    if (!createDirs(outDir))
    {
        status("Failed to create output directory!");
        setProgressBarState(100, false, true);
        return;
    }

    QString outSuffix = QFileInfo(inputFile).suffix();
    QString outPrefix = ui->leOutPrefix->text();
    if (outPrefix.trimmed().isEmpty())
    {
        outPrefix = QFileInfo(inputFile).completeBaseName();
    }

    splitterThread = new SplitterThread(this);
    splitterThread->setOutPrefix(outPrefix);
    splitterThread->setOutSuffix(outSuffix);
    splitterThread->setInputFile(inputFile);
    splitterThread->setOutputDir(outDir);
    splitterThread->setChunkSize(chunkSize);

    connect(splitterThread, &SplitterThread::finished, splitterThread, &QObject::deleteLater);
    connect(splitterThread, &SplitterThread::processCompleted, this, &MainWindow::splitThreadFinished);
    connect(splitterThread, &SplitterThread::progress, this, &MainWindow::updateProgress);
    connect(splitterThread, &SplitterThread::error, this, &MainWindow::handleError);
    connect(splitterThread, &SplitterThread::aborted, this, &MainWindow::operationAborted);

    splitterThread->start();
    disableOperations(true, 0);
    setProgressBarState(0, true, false);
}

void MainWindow::bRunMergeClick()
{
    if (mergerThread)
    {
        if (mergerThread->isRunning())
        {
            mergerThread->stop();
            ui->bRunMerge->setText("Stopping...");
            ui->bRunMerge->setDisabled(true);
            status("Stopping and cleaning up...");
            return;
        }
    }

    QStringList inputFiles = mergeFilesModel->stringList();
    QString outputFile = ui->leMergeOutPath->text();

    if (inputFiles.isEmpty())
    {
        status("Please add files to merge!");
        return;
    }
    for (QString inFi : inputFiles)
    {
        if (!QFileInfo::exists(inFi))
        {
            status("One of the input file is invalid!");
            return;
        }
    }
    if (outputFile.trimmed().isEmpty())
    {
        status("Please provide a valid output file path!");
        return;
    }

    status("");

    QString outAbsolute = QFileInfo(outputFile).absoluteFilePath();
    for (const QString &inFile : inputFiles)
    {
        if (QFileInfo(inFile).absoluteFilePath() == outAbsolute)
        {
            status("Error: Output file cannot be the same as any input file!");
            return;
        }
    }

    if (QFileInfo::exists(outputFile)) {
        QMessageBox::StandardButton reply = QMessageBox::warning(this, "File exists", 
            "The output merged file already exists. Do you want to overwrite it?", QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No) {
            return;
        }
    }

    mergerThread = new MergerThread(this);
    mergerThread->setInputFiles(inputFiles);
    mergerThread->setOutputFile(outputFile);

    connect(mergerThread, &MergerThread::finished, mergerThread, &QObject::deleteLater);
    connect(mergerThread, &MergerThread::processCompleted, this, &MainWindow::mergeThreadFinished);
    connect(mergerThread, &MergerThread::progress, this, &MainWindow::updateProgress);
    connect(mergerThread, &MergerThread::error, this, &MainWindow::handleError);
    connect(mergerThread, &MergerThread::aborted, this, &MainWindow::operationAborted);

    mergerThread->start();
    disableOperations(true, 1);
    setProgressBarState(0, true, false);
}

void MainWindow::splitThreadFinished()
{
    disableOperations(false);
    status("Splitting Finished");
    setProgressBarState(100, false, false);
}

void MainWindow::mergeThreadFinished()
{
    disableOperations(false);
    status("Merging Finished");
    setProgressBarState(100, false, false);
}

void MainWindow::operationAborted()
{
    disableOperations(false);
    setProgressBarState(0, false, false);
    status("Operation Stopped");
}

void MainWindow::bBrowseInClick()
{
    QString p = ui->leSplitPath->text();
    QString filename = QFileDialog::getOpenFileName(this, tr("Select a file"), p.isEmpty() ? QDir::currentPath() : p);
    if (!filename.isNull())
    {
        ui->leSplitPath->setText(filename);
    }
}

void MainWindow::bBrowseOutClick()
{
    QString dirname = QFileDialog::getExistingDirectory(this, tr("Select a Directory"), QDir::currentPath());
    if (!dirname.isNull())
    {
        ui->leSplitOutDir->setText(dirname);
    }
}

void MainWindow::bAddMergeInClick()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Select Files"), "", tr("All Files (*)"));

    addFilesToMergeList(files);
}

void MainWindow::bRemoveMergeInClick()
{
    QItemSelectionModel *selectionModel = ui->listViewMergePath->selectionModel();
    QModelIndexList selectedIndexes = selectionModel->selectedIndexes();

    if (!selectedIndexes.isEmpty())
    {
        QStringList currentFiles = mergeFilesModel->stringList();
        std::sort(selectedIndexes.begin(), selectedIndexes.end(), [](const QModelIndex &a, const QModelIndex &b) {
            return a.row() > b.row();
        });

        for (const QModelIndex &index : selectedIndexes)
        {
            mergeFilesModel->removeRow(index.row());
        }
    }
}

void MainWindow::bBrowseMergeOutClick()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"), "", tr("All Files (*)"));
    if (!fileName.isEmpty())
    {
        ui->leMergeOutPath->setText(fileName);
    }
}

void MainWindow::status(const QString &msg)
{
    ui->statusBar->showMessage(msg.isEmpty() ? "" : "[" + msg + "]");
}

QString MainWindow::formatPath(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

bool MainWindow::createDirs(const QString &path)
{
    // Normalize path to handle edge cases like "C://"
    QString cleanPath = QDir::cleanPath(path);
    QDir dir;

    // Check existence first to prevent mkpath from failing on root drives
    if (dir.exists(cleanPath))
    {
        return true;
    }
    return dir.mkpath(cleanPath);
}

void MainWindow::disableOperations(bool isDisabled, int activeOperation)
{
    ui->leSplitPath->setDisabled(isDisabled);
    ui->leSplitOutDir->setDisabled(isDisabled);
    ui->leOutPrefix->setDisabled(isDisabled);
    ui->leChunkSize->setDisabled(isDisabled);
    ui->bBrowseIn->setDisabled(isDisabled);
    ui->bBrowseOut->setDisabled(isDisabled);
    ui->cbUnit->setDisabled(isDisabled);
    ui->leMergeOutPath->setDisabled(isDisabled);
    ui->bAddMergeIn->setDisabled(isDisabled);
    ui->bRemoveMergeIn->setDisabled(isDisabled);
    ui->bBrowseMergeOut->setDisabled(isDisabled);

    if (isDisabled)
    {
        if (activeOperation == 0)
        { // Split
            ui->bRunSplit->setText("Stop");
            ui->bRunSplit->setDisabled(false);
            ui->bRunMerge->setDisabled(true);
        }
        else if (activeOperation == 1)
        { // Merge
            ui->bRunMerge->setText("Stop");
            ui->bRunMerge->setDisabled(false);
            ui->bRunSplit->setDisabled(true);
        }
    }
    else
    {
        ui->bRunSplit->setText("Run");
        ui->bRunSplit->setDisabled(false);
        ui->bRunMerge->setText("Run");
        ui->bRunMerge->setDisabled(false);
    }
}

void MainWindow::setProgressBarState(int value, bool isIndeterminate, bool isError)
{
    if (isIndeterminate)
    {
        ui->progressBar->setRange(0, 0);
    }
    else
    {
        ui->progressBar->setRange(0, 100);
        ui->progressBar->setValue(value);
    }
    if (isError)
    {
        ui->progressBar->setStyleSheet("QProgressBar::chunk { background-color: red; }");
    }
    else
    {
        ui->progressBar->setStyleSheet("");
    }
}

void MainWindow::updateProgress(int value)
{
    setProgressBarState(value, false, false);
}
