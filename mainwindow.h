#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMessageBox>

#include "gdal_priv.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButtonDem_clicked();

    void on_pushButtonClass_clicked();

    void on_pushButtonHabitat_clicked();

    void on_pushButtonOk_clicked();

    void on_pushButtonCancel_clicked();

    void on_comboBoxLayer_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;
    QMessageBox msgBox;
    int blockSize;
};
#endif // MAINWINDOW_H
