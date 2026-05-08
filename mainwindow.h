#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStringListModel>
#include <QPointer>
#include "splitterthread.h"
#include "mergerthread.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    
private:
    Ui::MainWindow *ui;
    QPointer<SplitterThread> splitterThread;
    QPointer<MergerThread> mergerThread;
    QStringListModel *mergeFilesModel;

    void status(const QString &msg);
    QString formatPath(const QString &path);
    bool createDirs(const QString &path);
    void disableOperations(bool isDisabled, int activeOperation = -1);
    void setProgressBarState(int value, bool isIndeterminate, bool isError);
    void addFilesToMergeList(const QStringList &newFiles);

private slots:
    void bRunSplitClick();
    void bRunMergeClick();
    void bBrowseInClick();
    void bBrowseOutClick();
    void bAddMergeInClick();
    void bRemoveMergeInClick();
    void bBrowseMergeOutClick();
    void splitThreadFinished();
    void mergeThreadFinished();
    void updateProgress(int value);
    void handleError(const QString &message);
    void operationAborted();
};

#endif // MAINWINDOW_H
