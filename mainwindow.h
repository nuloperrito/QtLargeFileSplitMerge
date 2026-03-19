#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QStringListModel>
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
    SplitterThread *splitterThread;
    MergerThread *mergerThread;
    QStringListModel *mergeFilesModel;

    void status(QString path);
    QString formatPath(QString path);
    bool createDirs(QString path);
    void disableOperations(bool isDisabled);
    void setProgressBarState(int value, bool isIndeterminate, bool isError);

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
};

#endif // MAINWINDOW_H
