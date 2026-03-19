#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QShortcut>
#include <cmath>

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

    new QShortcut(QKeySequence("F5"), this, SLOT(bRunSplitClick()));
    new QShortcut(QKeySequence("F2"), this, SLOT(bBrowseInClick()));
    new QShortcut(QKeySequence("F3"), this, SLOT(bBrowseOutClick()));
    new QShortcut(QKeySequence("Esc"), this, SLOT(close()));

    setProgressBarState(0, false, false);
    setWindowTitle("File Splitter & Merger");
    setFixedSize(this->width(), this->height());
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    ui->cbUnit->setCurrentIndex(1);
}

MainWindow::~MainWindow()
{
    if (splitterThread)
    {
        splitterThread->stop();
        splitterThread->wait();
        delete splitterThread;
    }
    if (mergerThread)
    {
        mergerThread->stop();
        mergerThread->wait();
        delete mergerThread;
    }
    delete ui;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls()) {
        return;
    }

    QList<QUrl> urls = mimeData->urls();
    int currentTab = ui->tabWidget->currentIndex();

    if (currentTab == 0) { 
        // Split Tab Logic
        if (urls.size() != 1) {
            return; // Only accept single file or directory
        }
        
        QFileInfo fileInfo(urls.first().toLocalFile());
        if (fileInfo.isFile()) {
            ui->leSplitPath->setText(fileInfo.absoluteFilePath());
        } else if (fileInfo.isDir()) {
            ui->leSplitOutDir->setText(fileInfo.absoluteFilePath());
        }
    } 
    else if (currentTab == 1) { 
        // Merge Tab Logic
        QStringList newFiles;
        for (const QUrl &url : urls) {
            QFileInfo fileInfo(url.toLocalFile());
            if (fileInfo.isFile()) { // Exclude directories
                newFiles.append(fileInfo.absoluteFilePath());
            }
        }

        if (!newFiles.isEmpty()) {
            // Sort files alphabetically by file name
            std::sort(newFiles.begin(), newFiles.end(), [](const QString &a, const QString &b) {
                return QFileInfo(a).fileName() < QFileInfo(b).fileName();
            });

            QStringList currentFiles = mergeFilesModel->stringList();
            currentFiles.append(newFiles);
            mergeFilesModel->setStringList(currentFiles);
        }
    }
    
    event->acceptProposedAction();
}

void MainWindow::bRunSplitClick()
{
    if (splitterThread && splitterThread->isRunning())
    {
        splitterThread->stop();
        return;
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
    int unit_idx = ui->cbUnit->currentIndex();
    chunkSize *= (quint64)pow(1024, unit_idx + 1);

    status("");

    outDir = formatPath(outDir) + "/";
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

    splitterThread = new SplitterThread();
    splitterThread->setInputFile(inputFile);
    splitterThread->setOutputDir(outDir);
    splitterThread->setOutputPattern(outPrefix + "_%1." + outSuffix);
    splitterThread->setChunkSize(chunkSize);

    connect(splitterThread, &SplitterThread::finished, this, &MainWindow::splitThreadFinished);
    connect(splitterThread, &SplitterThread::progress, this, &MainWindow::updateProgress);

    splitterThread->start();
    disableOperations(true);
    setProgressBarState(0, true, false);
}

void MainWindow::bRunMergeClick()
{
    if (mergerThread && mergerThread->isRunning())
    {
        mergerThread->stop();
        return;
    }

    QStringList inputFiles = mergeFilesModel->stringList();
    QString outputFile = ui->leMergeOutPath->text();

    if (inputFiles.isEmpty())
    {
        status("Please add files to merge!");
        return;
    }
    for(QString inFi : inputFiles)
    {
        if(!QFileInfo::exists(inFi))
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

    mergerThread = new MergerThread();
    mergerThread->setInputFiles(inputFiles);
    mergerThread->setOutputFile(outputFile);

    connect(mergerThread, &MergerThread::finished, this, &MainWindow::mergeThreadFinished);
    connect(mergerThread, &MergerThread::progress, this, &MainWindow::updateProgress);

    mergerThread->start();
    disableOperations(true);
    setProgressBarState(0, true, false);
}

void MainWindow::splitThreadFinished()
{
    disableOperations(false);
    status("Splitting Finished");
    setProgressBarState(100, false, false);

    if (splitterThread)
    {
        splitterThread->deleteLater();
        splitterThread = nullptr;
    }
}

void MainWindow::mergeThreadFinished()
{
    disableOperations(false);
    status("Merging Finished");
    setProgressBarState(100, false, false);

    if (mergerThread)
    {
        mergerThread->deleteLater();
        mergerThread = nullptr;
    }
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

    if (!files.isEmpty())
    {
        QStringList currentFiles = mergeFilesModel->stringList();
        currentFiles.append(files);
        mergeFilesModel->setStringList(currentFiles);
    }
}

void MainWindow::bRemoveMergeInClick()
{
    QItemSelectionModel *selectionModel = ui->listViewMergePath->selectionModel();
    QModelIndexList selectedIndexes = selectionModel->selectedIndexes();

    if (!selectedIndexes.isEmpty())
    {
        QStringList currentFiles = mergeFilesModel->stringList();
        for (const QModelIndex &index : selectedIndexes)
        {
            currentFiles.removeAt(index.row());
        }
        mergeFilesModel->setStringList(currentFiles);
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

void MainWindow::status(QString msg)
{
    ui->statusBar->showMessage(msg.isEmpty() ? "" : "[" + msg + "]");
}

QString MainWindow::formatPath(QString path)
{
    return QFileInfo(path).absoluteFilePath();
}

bool MainWindow::createDirs(QString path)
{
    QDir dir;
    return dir.mkpath(path);
}

void MainWindow::disableOperations(bool isDisabled)
{
    ui->bRunSplit->setText(isDisabled ? "Stop" : "Run");
    ui->leSplitPath->setDisabled(isDisabled);
    ui->leSplitOutDir->setDisabled(isDisabled);
    ui->leOutPrefix->setDisabled(isDisabled);
    ui->leChunkSize->setDisabled(isDisabled);
    ui->bBrowseIn->setDisabled(isDisabled);
    ui->bBrowseOut->setDisabled(isDisabled);
    ui->cbUnit->setDisabled(isDisabled);
    ui->bRunMerge->setDisabled(isDisabled);
    ui->leMergeOutPath->setDisabled(isDisabled);
    ui->bAddMergeIn->setDisabled(isDisabled);
    ui->bRemoveMergeIn->setDisabled(isDisabled);
    ui->bBrowseMergeOut->setDisabled(isDisabled);
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
