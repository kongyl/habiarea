#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QScreen>
#include <QFileDialog>
#include <QProcess>
#include <QProgressDialog>
#include <QDebug>

#include "ogrsf_frmts.h"
#include "cpl_conv.h"
#include "gdal_alg.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // fix window size
    setWindowFlags(windowFlags()& ~Qt::WindowMaximizeButtonHint);
    setFixedSize(this->width(), this->height());
    // show on the center
    QScreen *screen = QApplication::primaryScreen();
    move((screen->geometry().width() - this->width()) / 2, (screen->geometry().height() - this->height()) / 2);

    GDALAllRegister();
    blockSize = 2048;
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::on_pushButtonDem_clicked()
{
    ui->lineEditDem->setText(QFileDialog::getOpenFileName(this, QString(), QString(), "GeoTiff(*.tif *.tiff)"));
}


void MainWindow::on_pushButtonClass_clicked()
{
    ui->lineEditClass->setText(QFileDialog::getOpenFileName(this, QString(), QString(), "GeoTiff(*.tif *.tiff)"));
}


void MainWindow::on_pushButtonHabitat_clicked()
{
    QString gdbPath = QFileDialog::getExistingDirectory();
    ui->lineEditHabitat->setText(gdbPath);
    if (gdbPath.isEmpty())
    {
        ui->lineEditHabitat->setText("");
        ui->comboBoxLayer->clear();
        ui->comboBoxExist->clear();
        return;
    }

    // check gdb
    GDALDataset *gdbDs = static_cast<GDALDataset*>(GDALOpenEx(gdbPath.toStdString().c_str(), GDAL_OF_VECTOR, NULL, NULL, NULL));
    if(gdbDs == nullptr)
    {
        msgBox.critical(this, QStringLiteral("File error"), QStringLiteral("Please choose a correct FileGDB"));
        ui->lineEditHabitat->setText("");
        ui->comboBoxLayer->clear();
        return;
    }

    for (int i = 0; i < gdbDs->GetLayerCount(); i++)
    {
        OGRLayer *layer = gdbDs->GetLayer(i);
        QString name(layer->GetName());
        ui->comboBoxLayer->addItem(name);
    }
    GDALClose(gdbDs);
}


void MainWindow::on_pushButtonOk_clicked()
{
    // update control
    ui->pushButtonOk->setDisabled(true);

    // check file
    QString demPath = ui->lineEditDem->text();
    QString classPath = ui->lineEditClass->text();
    QString habitatPath = ui->lineEditHabitat->text();
    if (demPath.isEmpty())
    {
        msgBox.critical(this, QStringLiteral("Path error"), QStringLiteral("Please choose a DEM raster"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (classPath.isEmpty())
    {
        msgBox.critical(this, QStringLiteral("Path error"), QStringLiteral("Please choose a class raster"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (habitatPath.isEmpty())
    {
        msgBox.critical(this, QStringLiteral("Path error"), QStringLiteral("Please choose a habitat vector"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }

    // open dem
    GDALDataset *demDs = (GDALDataset*)GDALOpen(demPath.toStdString().c_str(), GA_ReadOnly);
    if (demDs == NULL)
    {
        msgBox.critical(this, QStringLiteral("File error"), QStringLiteral("Open DEM failed"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    double geoTrans[6];
    demDs->GetGeoTransform(geoTrans);
    const char *proj = demDs->GetProjectionRef();
    GDALRasterBand *demBand = demDs->GetRasterBand(1);
    int xSize = demBand->GetXSize();
    int ySize = demBand->GetYSize();
    double xMin = geoTrans[0];
    double xMax = geoTrans[0] + geoTrans[1] * xSize;
    double yMin = geoTrans[3] + geoTrans[5] * ySize;
    double yMax = geoTrans[3];

    // open class
    GDALDataset *classDs = (GDALDataset*)GDALOpen(classPath.toStdString().c_str(), GA_ReadOnly);
    if (classDs == NULL)
    {
        GDALClose((GDALDatasetH) demDs);
        msgBox.critical(this, QStringLiteral("File error"), QStringLiteral("Open class raster failed"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    GDALRasterBand *classBand = classDs->GetRasterBand(1);
    if (classBand->GetXSize() != xSize || classBand->GetYSize() != ySize)
    {
        GDALClose((GDALDatasetH) classDs);
        GDALClose((GDALDatasetH) demDs);
        msgBox.critical(this, QStringLiteral("File error"), QStringLiteral("The size of class raster must match the DEM"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }

    // open habitat
    GDALDataset *habitatDs = static_cast<GDALDataset*>(GDALOpenEx(habitatPath.toStdString().c_str(), GDAL_OF_UPDATE, NULL, NULL, NULL));
    if (habitatDs == NULL)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        msgBox.critical(this, QStringLiteral("File error"), QStringLiteral("Open habitat vector failed"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    OGRLayer *habitatLayer = habitatDs->GetLayerByName(ui->comboBoxLayer->currentText().toStdString().c_str());

    // check fields
    OGRFeatureDefn *defn = habitatLayer->GetLayerDefn();
    if (defn->GetFieldIndex("altitude_low") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named altitude_low"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("altitude_upper") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named altitude_upper"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("h1") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named h1"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("h2") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named h2"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("h3") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named h3"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("h4") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named h4"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("h5") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named h5"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("h6") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named h6"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("h7") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named h7"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }
    if (defn->GetFieldIndex("h8") < 0)
    {
        GDALClose((GDALDatasetH) demDs);
        GDALClose((GDALDatasetH) classDs);
        GDALClose(habitatDs);
        msgBox.critical(this, QStringLiteral("Data error"), QStringLiteral("The layer must contain a field named h8"));
        ui->pushButtonOk->setDisabled(false);
        return;
    }

    // check field
    QString fieldName;
    if (ui->radioButtonExist->isChecked())
    {
        if (ui->comboBoxExist->count() <= 0)
        {
            GDALClose((GDALDatasetH) demDs);
            GDALClose((GDALDatasetH) classDs);
            GDALClose(habitatDs);
            msgBox.critical(this, QStringLiteral("Field error"), QStringLiteral("There is not existing field can store aoh"));
            ui->pushButtonOk->setDisabled(false);
            return;
        }
        fieldName = ui->comboBoxExist->currentText();
    }
    if (ui->radioButtonAdd->isChecked())
    {
        fieldName = ui->lineEditAdd->text();
        if (fieldName.isEmpty())
        {
            GDALClose((GDALDatasetH) demDs);
            GDALClose((GDALDatasetH) classDs);
            GDALClose(habitatDs);
            msgBox.critical(this, QStringLiteral("Field error"), QStringLiteral("Please add a field name to store aoh"));
            ui->pushButtonOk->setDisabled(false);
            return;
        }
        if (defn->GetFieldIndex(fieldName.toStdString().c_str()) >= 0)
        {
            GDALClose((GDALDatasetH) demDs);
            GDALClose((GDALDatasetH) classDs);
            GDALClose(habitatDs);
            msgBox.critical(this, QStringLiteral("Field error"), QStringLiteral("Field has existed"));
            ui->pushButtonOk->setDisabled(false);
            return;
        }
        OGRFieldDefn fieldAoh(fieldName.toStdString().c_str(), OFTInteger);
        OGRErr fieldErr = habitatLayer->CreateField(&fieldAoh);
        if (fieldErr != OGRERR_NONE)
        {
            GDALClose((GDALDatasetH) demDs);
            GDALClose((GDALDatasetH) classDs);
            GDALClose(habitatDs);
            msgBox.critical(this, QStringLiteral("Field error"), QStringLiteral("Create Field failed"));
            ui->pushButtonOk->setDisabled(false);
            return;
        }
    }

    // close
    GDALClose((GDALDatasetH) demDs);
    GDALClose((GDALDatasetH) classDs);
    GDALClose(habitatDs);
    //proDialog.setValue(featureCount);
    msgBox.information(this, QStringLiteral("mission complete"), QStringLiteral("mission complete"));
    ui->pushButtonOk->setDisabled(false);
}


void MainWindow::on_pushButtonCancel_clicked()
{
    this->close();
}


void MainWindow::on_comboBoxLayer_currentIndexChanged(int index)
{
    ui->comboBoxExist->clear();
    QString gdbPath = ui->lineEditHabitat->text();
    if (gdbPath.isEmpty())
    {
        return;
    }

    GDALDataset *gdbDs = static_cast<GDALDataset*>(GDALOpenEx(gdbPath.toStdString().c_str(), GDAL_OF_VECTOR, NULL, NULL, NULL));
    OGRLayer *layer = gdbDs->GetLayer(index);
    OGRFeatureDefn *defn = layer->GetLayerDefn();
    for (int i = 0; i < defn->GetFieldCount(); i++)
    {
        OGRFieldDefn *field = defn->GetFieldDefn(i);
        OGRFieldType type = field->GetType();
        if (type == OFTInteger)
        {
            ui->comboBoxExist->addItem(field->GetNameRef());
        }
    }
    if (ui->comboBoxExist->count() <= 0)
    {
        ui->radioButtonAdd->setChecked(true);
    }
    GDALClose(gdbDs);
}

