#include "MainWindow.hpp"
#include "ui_MainWindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent), ui(new Ui::MainWindow)
{
    // GUI Configuration
    this->ui->setupUi(this);

    // Check if the database exists otherwise the database is created
    ResourceInstaller dbInstaller("EcoMotion", "EcoManager2013");
    if (!dbInstaller.CheckSettings())
        dbInstaller.InstallSettings(QDir::current(), "EcoMotion2013.db");

    // Building of the parts of the MainWindow
    this->createRaceView();
    this->createRaceTable();
    this->createMapZone();
    this->createPlotZone();
    this->createMegaSquirtZone();
    this->createToolsBar();

    // Connect all the signals
    this->connectSignals();

    // Display Configuration
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    this->readSettings("MainWindow");
    this->centerOnScreen();
}

MainWindow::~MainWindow(void)
{
    delete this->ui;
}

/* Interception des events clavier emis sur la sectorView afin de pouvoir repercuter
 * la suppression logique d'un secteur sur la BD */
bool MainWindow::eventFilter(QObject* src, QEvent* event)
{
    if (src == this->ui->sectorView && event->type() == QEvent::KeyRelease)
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Delete)
        {
            // Check if at lease one sector is selected
            QModelIndexList selectedRows = this->ui->sectorView->selectionModel()->selectedRows();
            if (selectedRows.isEmpty())
            {
                QMessageBox::information(this,
                                       tr("Impossible de supprimer un secteur"),
                 tr("Vous devez sélectionner au moins un secteur à supprimer"));

                return QMainWindow::eventFilter(src, event);
            }

            QModelIndex selectedSector = selectedRows.first();

            if (selectedSector.isValid())
            {
                QSqlRecord sectorRec = sectorModel->record(selectedSector.row());
                int numSect = sectorRec.value(sectorModel->fieldIndex("num")).toInt();
                mapScene->mergeSector(numSect - 1, numSect);
            }
        }
    }

    return QMainWindow::eventFilter(src, event);
}

void MainWindow::on_actionAboutQt_triggered(void)
{
    qApp->aboutQt();
}

void MainWindow::on_actionQuit_triggered(void)
{
    // Save the state of the mainWindow and its widgets
    this->writeSettings("MainWindow");

    qApp->quit();
}

// importData
void MainWindow::on_actionImport_triggered(void)
{
    // Select the directory that content race data and enter race information
    CompetitionEntryDialog dial;
    QString raceDirectoryPath = QFileDialog::getExistingDirectory(this);
    if (raceDirectoryPath.isEmpty() || dial.exec() != QDialog::Accepted)
    {
        QMessageBox::information(this, tr("Importation annulée"),
                                 tr("L'importation des données de la course"
                                    " a été <strong>annulée</strong>"));
        return;
    }

    ImportModule raceInformationImporter;

    if (dial.isNewlyCreated())
    {
        // Create new entry for the competition in the database
        raceInformationImporter.createCompetition(dial.competitionName(),
                                                  dial.wheelRadius()/ 100.0,
                                                  dial.place());

        // Update combobox taht contains the list of competition names
        this->competitionNameModel->select();
    }

    Race newRace(dial.competitionName());
    newRace.setDate(dial.date());

    raceInformationImporter.addRace(newRace, raceDirectoryPath);
    if (!raceInformationImporter.importSuceed())
    {
        QMessageBox::warning(this, tr("Erreur d'importation"),
                             raceInformationImporter.getErrorString());
    }
    else
    {
        this->reloadRaceView();
    }

}

void MainWindow::on_actionAboutEcoManager2013_triggered(void)
{
    QMessageBox::information(this, "Action About EcoManager 2013",
                             "A propos du project EcoManager 2013 ....");
}

void MainWindow::on_actionExportConfigurationModule_triggered(void)
{
    QFileDialog dirChooser(this);
    dirChooser.setFileMode(QFileDialog::DirectoryOnly);

    if (dirChooser.exec() == QFileDialog::Accepted)
    {
        QStringList dirsPaths = dirChooser.selectedFiles();
        QDir selectedDir(dirsPaths.at(0));

        QStringList choices;
        QSqlQuery query("select name from COMPETITION");
        query.exec();

        while (query.next())
            choices << query.value(0).toString();

        QString nameCompet;
        bool ok;

        do
        {
            nameCompet = QInputDialog::getItem(this, tr("Nom de la compétition"),
                                               QString(), choices, 0, false, &ok);
        }
        while (! ok && nameCompet.isEmpty());

        if (ok)
            ExportModule::buildSectorOutput(nameCompet, selectedDir);
    }
}

void MainWindow::on_actionExportData_triggered(void)
{
    QMessageBox::information(this, "Action Export Data",
                             "Exportation de données ...");
}

/* Event that occured when the user double click on a race tree view item
 * Only lap can be double-clicked
 */
void MainWindow::on_raceView_doubleClicked(const QModelIndex& index)
{
    QModelIndex parent = index.parent();
    int depth(0);

    while (parent.isValid())
    {
        parent = parent.parent();
        depth++;
    }

    // check if a lap has been selected
    if (depth == 2)
        this->displayDataLap();
    else
        QMessageBox::information(this, tr("Erreur"),
                                 tr("Vous devez double-cliquer sur un tour"));
}

// chooseSampleLap
void MainWindow::on_actionDelimitingSectors_triggered(void)
{
    SampleLapViewer* dial = new SampleLapViewer(currentCompetition, this);
    dial->setModal(true);
    dial->resize(650, 400);

    // if the user choose a new sample lap
    if (dial->exec())
    {
        // Get track identifier
        QPair<int, int> refLap = dial->selectedReferencesLap();
        QSqlQuery posBoundaryQuery;
        posBoundaryQuery.prepare("select min(id), max(id) from POSITION where ref_lap_race = ? and ref_lap_num = ?");
        posBoundaryQuery.addBindValue(refLap.first);
        posBoundaryQuery.addBindValue(refLap.second);

        if (!posBoundaryQuery.exec() || ! posBoundaryQuery.next())
        {
            qWarning() << posBoundaryQuery.lastQuery() << posBoundaryQuery.lastError();
            return;
        }

        // Check if sectors alreay exists for the competition
        QSqlQuery existingSectorQuery;
        existingSectorQuery.prepare("select count(*) from SECTOR where ref_compet = ?");
        existingSectorQuery.addBindValue(currentCompetition);

        if (existingSectorQuery.exec() && existingSectorQuery.next())
        {
            int nbExistingSectors = existingSectorQuery.value(0).toInt();

            if (nbExistingSectors > 0)
            {
                // delete all existing sectors for the competition
                QSqlQuery delExistingSectorsQuery;
                delExistingSectorsQuery.prepare("delete from SECTOR where ref_compet = ?");
                delExistingSectorsQuery.addBindValue(currentCompetition);

                if (!delExistingSectorsQuery.exec())
                {
                    qWarning() << delExistingSectorsQuery.lastQuery() << delExistingSectorsQuery.lastError();
                    return;
                }
            }

            // Create new sector for the competition
            QSqlQuery sectorCreationQuery;
            sectorCreationQuery.prepare("insert into SECTOR (num, ref_compet, start_pos, end_pos) values (?, ?, ?, ?)");
            sectorCreationQuery.addBindValue(0);
            sectorCreationQuery.addBindValue(currentCompetition);
            sectorCreationQuery.addBindValue(posBoundaryQuery.value(0).toInt());
            sectorCreationQuery.addBindValue(posBoundaryQuery.value(1).toInt());

            if (!sectorCreationQuery.exec())
            {
                qWarning() << sectorCreationQuery.lastQuery() << sectorCreationQuery.lastError();
            }
            else
            {
                // Erase previous sectors from the map scene
                if (this->mapScene->hasSectors())
                    this->mapScene->clearSectors();

                // Display new sector
                loadSectors(currentCompetition);
            }
        }
        else
        {
            qWarning() << existingSectorQuery.lastQuery() << existingSectorQuery.lastError();
        }
    }
}

void MainWindow::on_raceView_customContextMenuRequested(const QPoint &pos)
{
    // Vérifier si l'élément sur lequel le clique est effectué est bien un tour
    QModelIndex curIndex = this->ui->raceView->selectionModel()->currentIndex();
    if (!curIndex.parent().parent().isValid())
        return;

    // Get the position where the menu will appear
    //QPoint globalPos = this->raceView->mapToGlobal(pos);

    // for QAbstractScrollArea and derived classes you would use:
    QPoint globalPos = this->ui->raceView->viewport()->mapToGlobal(pos);

    QList<QAction *> actions;
    actions.append(this->ui->actionExportTrackDataInCSV);

    if (actions.count() > 0)
        QMenu::exec(actions, globalPos);
}

void MainWindow::on_actionExportTrackDataInCSV_triggered(void)
{
    QModelIndex curIndex = this->ui->raceView->selectionModel()->currentIndex();

    QMessageBox::information(this, "Exportation des données",
              "tour correctement récupéré --> Exportation dans un fichier CSV");
}

void MainWindow::on_actionDisplayRaceTableData_triggered(bool checked)
{
    this->ui->raceTable->setVisible(checked);
}

void MainWindow::on_actionDisplayRaceTableUnder_triggered()
{
    this->ui->MapPlotAndRaceSplitter->setOrientation(Qt::Vertical);
    this->ui->MapPlotAndRaceSplitter->insertWidget(1, this->ui->raceTable);
}

void MainWindow::on_actionDisplayRaceTableAbove_triggered()
{
    this->ui->MapPlotAndRaceSplitter->setOrientation(Qt::Vertical);
    this->ui->MapPlotAndRaceSplitter->insertWidget(0, this->ui->raceTable);
}

void MainWindow::on_actionDisplayRaceTableOnRight_triggered()
{
    this->ui->MapPlotAndRaceSplitter->setOrientation(Qt::Horizontal);
    this->ui->MapPlotAndRaceSplitter->insertWidget(1, this->ui->raceTable);
}

void MainWindow::on_actionDisplayRaceTableOnLeft_triggered()
{
    this->ui->MapPlotAndRaceSplitter->setOrientation(Qt::Horizontal);
    this->ui->MapPlotAndRaceSplitter->insertWidget(0, this->ui->raceTable);
}

void MainWindow::on_actionDisplayRaceView_triggered(bool checked)
{
    this->ui->raceFrame->setVisible(checked);
}

void MainWindow::on_actionSaveCurrentLayout_triggered(void)
{
    bool ok(false);

    QStringList listSavedLayouts;
    listSavedLayouts << this->ui->actionConfiguredLayout1->text()
                     << this->ui->actionConfiguredLayout2->text()
                     << this->ui->actionConfiguredLayout3->text()
                     << this->ui->actionConfiguredLayout4->text();

    QString layoutSelected = QInputDialog::getItem(
                this, tr("Sauvegarde de la disposition courante"),
                tr("Choisissez l'emplacement dans lequel sauver la disposition "
                   "courante"), listSavedLayouts, 0, false, &ok);

    if (!ok)
    {
        QMessageBox::warning(this, tr("Action annulée"),
                             tr("La sauvegarde de la disposition courante à été annulée"));
        return;
    }

    // Sauvegarde des paramètres d'affichage
    this->writeSettings(layoutSelected);
    QMessageBox::information(this, tr("Sauvegarde de la disposition courante"),
                             tr("La disposition courante a correctement été "
                                "sauvée dans ") + layoutSelected);
}

void MainWindow::on_actionConfiguredLayout1_triggered(void)
{
    this->readSettings(this->ui->actionConfiguredLayout1->text());
    //QMessageBox::information(this, "Choix d'une disposition", "Disposition 1 utilisée");
}

void MainWindow::on_actionConfiguredLayout2_triggered(void)
{
    this->readSettings(this->ui->actionConfiguredLayout2->text());
    //QMessageBox::information(this, "Choix d'une disposition", "Disposition 2 utilisée");
}

void MainWindow::on_actionConfiguredLayout3_triggered(void)
{
    this->readSettings(this->ui->actionConfiguredLayout3->text());
    //QMessageBox::information(this, "Choix d'une disposition", "Disposition 3 utilisée");
}

void MainWindow::on_actionConfiguredLayout4_triggered(void)
{
    this->readSettings(this->ui->actionConfiguredLayout4->text());
    //QMessageBox::information(this, "Choix d'une disposition", "Disposition 4 utilisée");
}

void MainWindow::loadCompetition(int index)
{
    this->currentCompetition = competitionNameModel->record(index).value(0).toString();

    this->clearAllData(); // Clear all tracks information of each view
    this->mapScene->clearSectors(); // clear sectors

    // clear the sector view
    if (this->sectorModel)
    {
        this->sectorModel->clear();
        this->ui->sectorView->update();
    }

    // Load races information
    this->reloadRaceView();
}

void MainWindow::removeSector(const QString &competitionName, int sectorNum)
{
    QSqlDatabase::database().driver()->beginTransaction();

    QSqlQuery delQuery("delete from sector where ref_compet = ? and num = ?");
    delQuery.addBindValue(competitionName);
    delQuery.addBindValue(sectorNum);

    if (delQuery.exec())
    {
        qDebug() << "sector deleted.";

        QSqlQuery updateNumSect("update SECTOR set num = num - 1 where ref_compet = ? and num > ?");
        updateNumSect.addBindValue(competitionName);
        updateNumSect.addBindValue(sectorNum);

        if (!updateNumSect.exec())
        {
            qWarning() << updateNumSect.lastError();
            QSqlDatabase::database().driver()->rollbackTransaction();
        }
        else
        {
            QSqlDatabase::database().driver()->commitTransaction();
            qDebug() << "sector effectively deleted.";
        }
    }
    else
    {
        qWarning() << "Unable to delete sector" << delQuery.lastError();
        QSqlDatabase::database().driver()->rollbackTransaction();
    }

    this->sectorModel->select();
    this->ui->sectorView->setVisible(this->sectorModel->rowCount() > 0);
}

void MainWindow::addSector(QString competName, int sectNum,
                           IndexedPosition firstCoord, IndexedPosition lastCoord)
{
    QSqlDatabase::database().driver()->beginTransaction();

    QSqlQuery updateNumSect("update SECTOR set num = num + 1 where ref_compet = ? and num >= ?");
    updateNumSect.addBindValue(competName);
    updateNumSect.addBindValue(sectNum);

    if (!updateNumSect.exec())
    {
        QSqlDatabase::database().driver()->rollbackTransaction();
    }
    else
    {
        QSqlQuery insertQuery("insert into SECTOR (num, ref_compet, start_pos, end_pos) values (?, ?, ?, ?)");
        insertQuery.addBindValue(sectNum);
        insertQuery.addBindValue(competName);
        insertQuery.addBindValue(firstCoord.index());
        insertQuery.addBindValue(lastCoord.index());

        if (insertQuery.exec())
        {
            QSqlDatabase::database().driver()->commitTransaction();
        }
        else
        {
            qWarning() << "Unable to insert new sector after : " << insertQuery.lastError();
            QSqlDatabase::database().driver()->rollbackTransaction();
        }
    }

    this->sectorModel->select();
    this->ui->sectorView->setVisible(true);
}

void MainWindow::updateSector(QString competName, int sectNum,
                          IndexedPosition firstCoord, IndexedPosition lastCoord)
{
    QSqlQuery updateBoundaries("update SECTOR set start_pos = ?, end_pos = ? where ref_compet = ? and num = ?");
    updateBoundaries.addBindValue(firstCoord.index());
    updateBoundaries.addBindValue(lastCoord.index());
    updateBoundaries.addBindValue(competName);
    updateBoundaries.addBindValue(sectNum);

    if (!updateBoundaries.exec())
        qWarning() << updateBoundaries.lastQuery() << updateBoundaries.lastError();
}

void MainWindow::clearAllData(void)
{
    this->mapScene->clearTracks();
    this->distancePlotFrame->scene()->clearCurves(); //this->distancePlotFrame->clearCurves();
    this->timePlotFrame->scene()->clearCurves(); //this->timePlotFrame->clearCurves();

    // Clear the list of all tracks currently displayed
    this->currentTracksDisplayed.clear();
}

void MainWindow::centerOnScreen(void)
{
    QDesktopWidget screen;

    QRect screenGeom = screen.screenGeometry(this);

    int screenCenterX = screenGeom.center().x();
    int screenCenterY = screenGeom.center().y();

    this->move(screenCenterX - width () / 2, screenCenterY - height() / 2);
}

void MainWindow::createRaceView(void)
{
    // Mainly developed with Qt Designer

    this->ui->raceView->expandAll();
    this->ui->raceView->resizeColumnToContents(0);
}

void MainWindow::createToolsBar(void)
{
    // Mainly developed with Qt Designer

    this->competitionNameModel = new QSqlTableModel(this);
    this->competitionNameModel->setTable("COMPETITION");
    this->competitionNameModel->removeRows(1, 2);

    // Create a comboBox used to selecting a competition
    this->competitionBox = new QComboBox();
    this->competitionBox->setEditable(false);
    this->competitionBox->setModel(this->competitionNameModel);
    this->competitionBox->setSizePolicy(QSizePolicy::Expanding,
                                        QSizePolicy::Expanding);

    // Add the comboBox to the toolBar of the MainWindow
    this->ui->mainToolBar->insertWidget(this->ui->actionDelimitingSectors,
                                        this->competitionBox);

    QObject::connect(this->competitionBox, SIGNAL(currentIndexChanged(int)),
                     this, SLOT(loadCompetition(int)));

    this->competitionNameModel->select();
}

void MainWindow::createMapZone(void)
{
    // Create the map scene
    this->mapScene = new MapScene(50 * 1000, this);

    // Create the map frame (the map view is included)
    this->mapFrame = new MapFrame(this);
    this->mapScene->attacheMapView(mapFrame->view());
    this->mapFrame->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

    // Manage the sector view
    this->ui->sectorView->setVisible(false);
    this->ui->sectorView->installEventFilter(this);

    // Manage the map splitter
    this->ui->mapSplitter->insertWidget(0, this->mapFrame);
    this->ui->mapSplitter->setStretchFactor(0, 3);

    // Manage sector model
    this->sectorModel = NULL;
}

void MainWindow::createPlotZone(void)
{
    // Create distance plot frame
    this->distancePlotFrame = new PlotFrame;
    HorizontalScale* distAxis = new HorizontalScale(Scale::Bottom);
    distAxis->setResolution(5);
    distAxis->setUnitLabel("d(m)");
    this->distancePlotFrame->addHorizontalAxis(distAxis);

    VerticalScale* dspeedAxis = new VerticalScale(Scale::Left);
    dspeedAxis->setResolution(1);
    dspeedAxis->setUnitLabel("v(km/h)");
    this->distancePlotFrame->addVerticalAxis(dspeedAxis);

    VerticalScale* daccAxis = new VerticalScale(Scale::Right);
    daccAxis->setResolution(1);
    daccAxis->translate(-50);
    daccAxis->scale(0.2);
    daccAxis->setUnitLabel("a(m/s²)");
    this->distancePlotFrame->addVerticalAxis(daccAxis);

    // Add the distance plot frame the the tab widget
    QVBoxLayout* distancePlotLayout = new QVBoxLayout(this->ui->tabDistance);
    distancePlotLayout->setMargin(0);
    distancePlotLayout->addWidget(this->distancePlotFrame);

    // Create time plot frame
    this->timePlotFrame = new PlotFrame;
    HorizontalScale* timeAxis = new HorizontalScale(Scale::Bottom);
    timeAxis->setResolution(5);
    timeAxis->setUnitLabel("t(s)");
    this->timePlotFrame->addHorizontalAxis(timeAxis);

    VerticalScale* tspeedAxis = new VerticalScale(Scale::Left);
    tspeedAxis->setResolution(1);
    tspeedAxis->setUnitLabel("v(km/h)");
    this->timePlotFrame->addVerticalAxis(tspeedAxis);

    VerticalScale* taccAxis = new VerticalScale(Scale::Right);
    taccAxis->setResolution(1);
    taccAxis->translate(-50);
    taccAxis->scale(0.2);
    taccAxis->setUnitLabel("a(m/s²)");
    this->timePlotFrame->addVerticalAxis(taccAxis);

    // Add the time plot frame the the tab widget
    QVBoxLayout* timePlotLayout = new QVBoxLayout(this->ui->tabTime);
    timePlotLayout->setMargin(0);
    timePlotLayout->addWidget(this->timePlotFrame);
}

void MainWindow::createMegaSquirtZone(void)
{
    // Create MegaSquirt plot frame
    this->megaSquirtPlotFrame = new PlotFrame;
    HorizontalScale* timeAxis = new HorizontalScale(Scale::Bottom);
    timeAxis->setResolution(5);
    timeAxis->setUnitLabel("t(s)");
    this->megaSquirtPlotFrame->addHorizontalAxis(timeAxis);

    // Add the plot frame to the megaSquirt splitter
    this->ui->megaSquirtSplitter->addWidget(this->megaSquirtPlotFrame);

    // Build model (model) for the comboBoxes (view/controler)
    QStringList megaSquirtParamList;
    megaSquirtParamList << "RPM" << "Batt V" << "PW" << "Gammae" << "Gair";
    QStringListModel* megaSquirtParamModel = new QStringListModel(megaSquirtParamList);

    // Apply the model to each comboBox
    this->ui->megaSquirtComboBox1->setModel(megaSquirtParamModel);
    this->ui->megaSquirtComboBox2->setModel(megaSquirtParamModel);
    this->ui->megaSquirtComboBox3->setModel(megaSquirtParamModel);
    this->ui->megaSquirtComboBox4->setModel(megaSquirtParamModel);
}

void MainWindow::createRaceTable(void)
{
    // Create the model for the table of laps information
    QList<QVariant> headers;
    headers << "Course" << "Tps(s)" << "Tps(s)" << "Dist(m)" << "v(km\\h)"
            << "Acc(m/s2)" << "RPM" << "PW";
    this->raceInformationTableModel = new TreeLapInformationModel(headers);

    // Use a proxy model to manage background color of each row
    LapInformationProxyModel* wrapper = new LapInformationProxyModel(this);
    wrapper->setSourceModel(this->raceInformationTableModel);

    // Apply the model to the table and change the selection mode
    this->ui->raceTable->setModel(wrapper);
    this->ui->raceTable->setSelectionMode(QAbstractItemView::MultiSelection);
}

void MainWindow::readSettings(const QString& settingsGroup)
{
//    QList<int> sizeList;

//    // The map zone and the plot zone have the same size
//    sizeList << this->ui->MapPlotSplitter->height() / 2  /* Map zone size */
//             << this->ui->MapPlotSplitter->height() / 2; /* Plots zone size */
//    this->ui->MapPlotSplitter->setSizes(sizeList);

//    // Hide the table of lap information
//    sizeList.clear();
//    sizeList << this->ui->MapPlotAndRaceSplitter->width() << 0;
//    this->ui->MapPlotAndRaceSplitter->setSizes(sizeList);

    QSettings settings;

    settings.beginGroup(settingsGroup);

    /* Contourne le bug non résolu par Qt de la restauration de la géométrie
     * d'une fenetre maximisée alors qu'elle est déjà maximisée */
    if (settings.value("isMaximized").toBool())
        this->showMaximized();
    else
        this->restoreGeometry(settings.value("geometry").toByteArray());

    this->ui->mainSplitter->restoreState(
                settings.value("mainSplitter").toByteArray());
    this->ui->MapPlotAndRaceSplitter->restoreState(
                settings.value("MapPlotAndRaceSplitter").toByteArray());
    this->ui->MapPlotSplitter->restoreState(
                settings.value("MapPlotSplitter").toByteArray());

    settings.endGroup();
}

void MainWindow::writeSettings(const QString& settingsGroup) const
{
    QSettings settings;

    settings.beginGroup(settingsGroup);
    settings.setValue("isMaximized", this->isMaximized());
    settings.setValue("geometry", this->saveGeometry());
    settings.setValue("mainSplitter", this->ui->mainSplitter->saveState());
    settings.setValue("MapPlotAndRaceSplitter", this->ui->MapPlotAndRaceSplitter->saveState());
    settings.setValue("MapPlotSplitter", this->ui->MapPlotSplitter->saveState());
    settings.endGroup();
}

void MainWindow::displayDataLap(void)
{
    QModelIndex curIndex = this->ui->raceView->selectionModel()->currentIndex();

    if (curIndex.parent().parent().isValid())
    {
        // Create trak identifier
        int ref_race = competitionModel->data(competitionModel->index(curIndex.row(), 1, curIndex.parent())).toInt();
        int ref_lap = competitionModel->data(competitionModel->index(curIndex.row(), 2, curIndex.parent())).toInt();

        QMap<QString, QVariant> trackIdentifier;
        trackIdentifier["race"] = ref_race;
        trackIdentifier["lap"] = ref_lap;

        // Check if the track is already displayed
        if (this->currentTracksDisplayed.contains(trackIdentifier))
        {
            QMessageBox::warning(this, tr("Opération impossible"),
                                 tr("Ce tour est déjà affiché dans les différentes vues"));
            return;
        }
        else
        {
            qDebug("tour pas encore affiché");
            this->currentTracksDisplayed.append(trackIdentifier);
        }

        /* ------------------------------------------------------------------ *
         *                         Populate map scene                         *
         * ------------------------------------------------------------------ */
        QSqlQuery posQuery("select longitude, latitude, timestamp from POSITION where ref_lap_race = ? and ref_lap_num = ? order by timestamp");
        posQuery.addBindValue(ref_race);
        posQuery.addBindValue(ref_lap);

        if (posQuery.exec())
        {
            QVector<QPointF> pos;
            QVector<float> indexValues;

            while (posQuery.next())
            {
                GeoCoordinate tmp;
                tmp.setLongitude(posQuery.value(0).toFloat());
                tmp.setLatitude(posQuery.value(1).toFloat());

                //see projection method doc for usage purpose
                pos.append(tmp.projection());
                indexValues << posQuery.value(2).toFloat() / 1000;
            }

            qDebug() << posQuery.lastQuery();
            qDebug() <<  ref_race << curIndex.row() << pos.size();
            this->mapScene->addTrack(pos, indexValues, trackIdentifier);
        }

        // If a sampling lap has already be defined, just load it in the view
        if (!this->mapScene->hasSectors())
            this->loadSectors(this->currentCompetition);

        /* ------------------------------------------------------------------ *
         *         Populate plot frames (play the role of plot scene )        *
         * ------------------------------------------------------------------ */
        QSqlQuery speedQuery;
        speedQuery.prepare("select timestamp, value from SPEED where ref_lap_race = ? and ref_lap_num = ? order by timestamp");
        speedQuery.addBindValue(ref_race);
        speedQuery.addBindValue(ref_lap);

        if (speedQuery.exec())
        {
            QList<IndexedPosition> distSpeedPoints; // liste des points de la vitesse par rapport à la distance
            QList<IndexedPosition> timeSpeedPoints; // Liste des points de la vitesse par rapport au temps
            QList<IndexedPosition> dAccPoints;      // Liste des points de l'accélération par rapport à la distance
            QList<IndexedPosition> tAccPoints;      // Liste des points de l'accélération par rapport au temps
            QPointF lastSpeed;
            QPointF lastValidSpeed;
            int count = 0;
            double lastPos = 0;

            while (speedQuery.next())
            {
                double time = speedQuery.value(0).toFloat() / 1000;
                double speed = speedQuery.value(1).toDouble();
                double pos;

                IndexedPosition dPoint, tPoint;
                dPoint.setIndex(time);
                tPoint.setIndex(time);

                if (count > 0)
                {
                    qreal diff = (speed - lastSpeed.y()) / 3.6;
                    qreal acc = diff / (time - lastSpeed.x());

                    if (qAbs(acc) < 2 && (time - lastSpeed.x()) < 1)
                    {
                        pos = lastPos + ((speed + lastSpeed.y()) / (2 * 3.6)) * (time - lastSpeed.x());
                        dPoint.setX(pos);
                        dPoint.setY(speed);
                        lastPos = pos;

                        tPoint.setX(time);
                        tPoint.setY(speed);
                        timeSpeedPoints << tPoint;

                        lastValidSpeed = QPointF(time, speed);
                        dAccPoints << IndexedPosition((pos + lastPos) / 2, acc, time);
                        tAccPoints << IndexedPosition((lastSpeed.x() + time) / 2, acc, time);
                    }
                    else
                    {
                        dPoint.setX(lastPos);
                        dPoint.setY(lastSpeed.y());
                        tPoint.setX(time);
                        tPoint.setY(lastSpeed.y());
                        timeSpeedPoints << tPoint;
                    }
                }
                else
                {
                    dPoint.setX(0);
                    dPoint.setY(speed);
                    lastValidSpeed = QPointF(time, speed);
                }

                distSpeedPoints << dPoint;
                lastSpeed = QPointF(time, speed);
                count++;
            }
            /*
            QList<QPointF> lineZero;

            if (! dAccPoints.isEmpty()) {
                lineZero << QPointF(dAccPoints.first().x(), 50) << QPointF(dAccPoints.last().x(), 50);
                PlotCurve* dc = distancePlotFrame->addCurve(dAccPoints, trackIdentifier);
                dc->translate(0, 50);
                dc->scale(1, 5);
                distancePlotFrame->addCurve(lineZero);

                lineZero.clear();
                lineZero << QPointF(tAccPoints.first().x(), 50) << QPointF(tAccPoints.last().x(), 50);
                PlotCurve* tc = timePlotFrame->addCurve(tAccPoints, trackIdentifier);
                tc->translate(0, 50);
                tc->scale(1, 5);
                timePlotFrame->addCurve(lineZero);
            }
*/
            this->distancePlotFrame->scene()->addCurve(distSpeedPoints, trackIdentifier);//this->distancePlotFrame->addCurve(distSpeedPoints, trackIdentifier);
            this->timePlotFrame->scene()->addCurve(timeSpeedPoints, trackIdentifier);//this->timePlotFrame->addCurve(timeSpeedPoints, trackIdentifier);

//            bool accelerationPositiv = false;

//            foreach (const IndexedPosition& ip, dAccPoints) {
//                qreal value = (ip.y() - 50) / 5;
//                qDebug() << value;
//                if (value > 0 && ! accelerationPositiv) {
//                    qDebug() << "+ transition found";
//                    mapScene->fixSymbol(ip.index(), QColor(0, 255, 0, 150), trackIdentifier);
//                    accelerationPositiv = true;
//                } else if (value < 0 && accelerationPositiv) {
//                    qDebug() << "- transition found";
//                    mapScene->fixSymbol(ip.index(), QColor(203, 0, 0, 150), trackIdentifier);
//                    accelerationPositiv = false;
//                }
//            }
//            if (tAccPoints.size() > 2) {
//                CurvePathBuilder builder(tAccPoints.at(0), tAccPoints.at(1), 1);

//                for (int i = 2; i < tAccPoints.size(); i++)
//                    builder.append(tAccPoints.at(i));
//                timePlotFrame->scene()->addPath(builder.exBound());
//            }

            /*
            QList<QVariant> raceInformation;
            for (int i(0); i < tAccPoints.count(); i++)
            {
                raceInformation.clear();

                // ajout des informations
                raceInformation.append("Course " + QString::number(ref_race) + " tour " + QString::number(ref_lap)); // course
                raceInformation.append(timeSpeedPoints.at(i).index()); // tps 1
                raceInformation.append(timeSpeedPoints.at(i).index()); // tps 2
                raceInformation.append(distSpeedPoints.at(i).x()); // distance
                raceInformation.append(timeSpeedPoints.at(i).y()); // vitesse
                raceInformation.append(tAccPoints.at(i).y()); // Acceleration
                raceInformation.append("RPM"); // RPM
                raceInformation.append("PW"); // PW

                this->raceInformationTableModel->addRaceInformation(raceInformation);
            }
            */


            QList<QVariant> raceInformation;
            for (int i(0); i < tAccPoints.count(); i++) // Environ 1000 éléments
            {
                raceInformation.clear();

                // ajout des informations
                raceInformation.append(QVariant());
                raceInformation.append(timeSpeedPoints.at(i).index()); // tps 1
                raceInformation.append(timeSpeedPoints.at(i).index()); // tps 2
                raceInformation.append(distSpeedPoints.at(i).x()); // distance
                raceInformation.append(timeSpeedPoints.at(i).y()); // vitesse
                raceInformation.append(tAccPoints.at(i).y()); // Acceleration
                raceInformation.append("RPM"); // RPM
                raceInformation.append("PW"); // PW

                this->raceInformationTableModel->addRaceInformation(
                            ref_race, ref_lap, raceInformation);
            }

        }

    }
}

void MainWindow::connectSignals(void)
{
    // Map scene
    connect(this->mapScene, SIGNAL(sectorRemoved(QString,int)),
            this, SLOT(removeSector(QString,int)));
    connect(this->mapScene, SIGNAL(sectorAdded(QString,int,IndexedPosition,IndexedPosition)),
            this, SLOT(addSector(QString,int,IndexedPosition,IndexedPosition)));
    connect(this->mapScene, SIGNAL(sectorUpdated(QString,int,IndexedPosition,IndexedPosition)),
            this, SLOT(updateSector(QString,int,IndexedPosition,IndexedPosition)));
    connect(this->mapScene, SIGNAL(pointSelected(float,QVariant)),
            this->distancePlotFrame->scene(), SLOT(highlightPoints(float,QVariant)));
    connect(this->mapScene, SIGNAL(pointSelected(float,QVariant)),
            this->timePlotFrame->scene(), SLOT(highlightPoints(float,QVariant)));
    connect(this->mapScene, SIGNAL(intervalSelected(float,float,QVariant)),
            this->distancePlotFrame->scene(), SLOT(highlightSector(float,float,QVariant)));
    connect(this->mapScene, SIGNAL(intervalSelected(float,float,QVariant)),
            this->timePlotFrame->scene(), SLOT(highlightSector(float,float,QVariant)));
    connect(this->mapScene, SIGNAL(selectionChanged()),
            this->distancePlotFrame->scene(), SLOT(clearPlotSelection()));
    connect(this->mapScene, SIGNAL(selectionChanged()),
            this->timePlotFrame->scene(), SLOT(clearPlotSelection()));

    // Map frame
    connect(this->mapFrame, SIGNAL(enableTrackHoverEvent(bool)),
            this->mapScene, SLOT(enableTrackAcceptHoverEvents(bool)));
    connect(this->mapFrame, SIGNAL(clearTracks()), this, SLOT(clearAllData()));

    // Distance plot frame
    connect(this->distancePlotFrame, SIGNAL(selectionChanged()),
            this->mapScene, SLOT(clearSceneSelection()));
    connect(this->distancePlotFrame->scene(), SIGNAL(pointSelected(float,QVariant)),
            this->mapScene, SLOT(highlightPoint(float,QVariant)));
    connect(this->distancePlotFrame->scene(), SIGNAL(intervalSelected(float,float,QVariant)),
            this->mapScene, SLOT(highlightSector(float,float,QVariant)));
    connect(this->distancePlotFrame, SIGNAL(clear()), this, SLOT(clearAllData()));

    // Time plot frame
    connect(this->timePlotFrame, SIGNAL(selectionChanged()),
            this->mapScene, SLOT(clearSceneSelection()));
    connect(this->timePlotFrame->scene(), SIGNAL(pointSelected(float,QVariant)),
            this->mapScene, SLOT(highlightPoint(float,QVariant)));
    connect(this->timePlotFrame->scene(), SIGNAL(intervalSelected(float,float,QVariant)),
            this->mapScene, SLOT(highlightSector(float,float,QVariant)));
    connect(this->timePlotFrame, SIGNAL(clear()), this, SLOT(clearAllData()));
}

void MainWindow::reloadRaceView(void)
{
    QSqlQueryModel* model = new QSqlQueryModel(this);
    QSqlQuery getAllLaps;
    getAllLaps.prepare("select date, race.num, lap.num, race.id, lap.num "
                       "from COMPETITION, RACE, LAP "
                       "where COMPETITION.name = ? and COMPETITION.name = RACE.ref_compet and RACE.id = LAP.ref_race "
                       "order by RACE.date, RACE.num, LAP.num");
    getAllLaps.addBindValue(this->currentCompetition);

    if (!getAllLaps.exec())
        return;

    model->setQuery(getAllLaps);

    this->competitionModel = new GroupingTreeModel(this);
    QList<int> cols;
    cols << 0 << 1; // ce par quoi on va grouper les éléments dans la vue (première colonne)
    this->competitionModel->setSourceModel(model, cols);

    CompetitionProxyModel* wrapper = new CompetitionProxyModel(this);
    wrapper->setSourceModel(this->competitionModel);

    QItemSelectionModel* oldSelectionModel = this->ui->raceView->selectionModel();
    if (oldSelectionModel == NULL)
        delete oldSelectionModel;

    this->ui->raceView->setModel(wrapper);
    this->ui->raceView->expandAll();
    this->ui->raceView->resizeColumnToContents(0);

    /* Do not show race.id or lap.num through view*/
    this->ui->raceView->setColumnHidden(1, true);
    this->ui->raceView->setColumnHidden(2, true);

//    connect(this->ui->raceView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), this, SLOT(competitionSelection(QItemSelection,QItemSelection)));
}

void MainWindow::loadSectors(const QString &competitionName)
{
    this->sectorModel = new QSqlTableModel(this);
    this->sectorModel->setTable("SECTOR");
    this->sectorModel->setEditStrategy(QSqlTableModel::OnFieldChange);
    this->sectorModel->setFilter(QString("ref_compet = '%1'").arg(competitionName));
    this->sectorModel->setSort(this->sectorModel->fieldIndex("num"), Qt::AscendingOrder);
    this->sectorModel->setHeaderData(3, Qt::Horizontal, tr("Vmin (km/h)"));
    this->sectorModel->setHeaderData(4, Qt::Horizontal, tr("Vmax (km/h)"));
    this->sectorModel->select();

    ColorizerProxyModel* coloredModel = new ColorizerProxyModel(6, this);
    coloredModel->setSourceModel(this->sectorModel);
    coloredModel->setIndexColumn(1);
    this->ui->sectorView->setModel(coloredModel);
    this->ui->sectorView->setColumnHidden(this->sectorModel->fieldIndex("id"), true);
    this->ui->sectorView->setColumnHidden(this->sectorModel->fieldIndex("num"), true);
    this->ui->sectorView->setColumnHidden(this->sectorModel->fieldIndex("ref_compet"), true);
    this->ui->sectorView->setColumnHidden(this->sectorModel->fieldIndex("start_pos"), true);
    this->ui->sectorView->setColumnHidden(this->sectorModel->fieldIndex("end_pos"), true);
    this->ui->sectorView->horizontalHeader()->setResizeMode(QHeaderView::Stretch);

    QSqlQuery posQuery;
    posQuery.prepare("select longitude, latitude, id from POSITION where id >= ? and id <= ?");
    int currentSector = 0;
    int startPosInd = this->sectorModel->fieldIndex("start_pos");
    int endPosInd = this->sectorModel->fieldIndex("end_pos");

    qDebug() << "sectors nb ; " << this->sectorModel->rowCount()  ;
    while (currentSector < this->sectorModel->rowCount())
    {
        posQuery.bindValue(0, this->sectorModel->record(currentSector).value(startPosInd));
        posQuery.bindValue(1, this->sectorModel->record(currentSector).value(endPosInd));

        if (posQuery.exec())
        {
            QVector<IndexedPosition> sectorPoints;

            while (posQuery.next())
            {
                GeoCoordinate tmp;
                tmp.setLongitude(posQuery.value(0).toFloat());
                tmp.setLatitude(posQuery.value(1).toFloat());
                sectorPoints.append(IndexedPosition(tmp.projection(), posQuery.value(2).toInt()));
            }

            if (! sectorPoints.isEmpty())
                this->mapScene->addSector(sectorPoints, competitionName);
        }
        currentSector++;
    }

    this->ui->sectorView->setVisible(mapScene->hasSectors());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Save the state of the mainWindow and its widgets
    this->writeSettings("MainWindow");

    QMainWindow::closeEvent(event);
}
