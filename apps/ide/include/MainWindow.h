#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QMap>
#include "WorkspaceScene.h"
#include "WorkspaceView.h"
#include "BlockEditor.h"
#include "HardwareSimulator.h"
#include "OscilloscopePanel.h"
#include <QTabWidget>

class ResistorItem;
class PotentiometerItem;
class CustomComponentItem;
class MotorItem;
class BessItem;
class DHT22Item;
class HCSR04Item;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() = default;

private slots:
    void onAddComponentClicked(QListWidgetItem* item);
    void onSelectionChanged(ComponentItem* selectedComp);
    void showComponentContextMenu(ComponentItem* comp, const QPointF& globalPos);
    void onToolboxContextMenu(const QPoint& pos);
    void editResistorValue(ResistorItem* resistor);
    void editPotentiometerValue(PotentiometerItem* potentiometer);
    void editMotorProperties(MotorItem* motor);
    void editBessProperties(BessItem* bess);
    void editCustomPotentiometerValue(CustomComponentItem* custom);
    void editDHT22Properties(DHT22Item* dht);
    void editHCSR04Properties(HCSR04Item* hcsr);
    void editMicrocontroller(ComponentItem* comp);
    void onComponentAdded(ComponentItem* comp);
    void showComponentModeling(ComponentItem* comp);

    
    // Core Toolbar actions
    void toggleSimulation();
    void compileCode();
    void buildProject();
    void clearScene();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    void exportLaserPNG();
    void viewCompiledCodeModal();
    void showFirmwareInfo();
    void platformIOConfigTriggered();

private:
    // Visual Panels
    WorkspaceScene* m_scene;
    WorkspaceView* m_view;
    BlockEditor* m_blockEditor;
    HardwareSimulator* m_simulator;
    
    QListWidget* m_toolboxList;
    QPlainTextEdit* m_compilerConsole;
    QPlainTextEdit* m_serialMonitor;
    QSplitter* m_mainSplitter;
    QTabWidget* m_bottomTabs = nullptr;
    OscilloscopePanel* m_oscilloscope = nullptr;
    QAction* m_playAction = nullptr;
    QAction* m_buildAction = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_newAction = nullptr;
    QAction* m_openAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_clearAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;

    // Active tracking
    ComponentItem* m_selectedComponent = nullptr;
    QString m_compiledCode;
    QString m_currentProjectPath;
    bool m_lastBuildOk = false;
    bool m_isBuilding = false;

    // Resource monitors
    QProgressBar* m_ramProgressBar = nullptr;
    QProgressBar* m_flashProgressBar = nullptr;
    QWidget* m_resourceBarWidget = nullptr;
    
    void buildLayout();
    void buildToolbar();
    void applyTheme();
    void checkAndInstallToolchain();
    void checkPythonAsync();
    void parseResourceUsage(const QString& line);
    void openEventEditor(ComponentItem* comp, const QString& eventName);
    void synchronizeLoopBlocks();
    void openComponentCreator();
    void loadToolboxItems();
    void preparePlatformIOProject();
    bool platformIOBuild();
    bool platformIOUpload();
    bool platformIOIsInstalled();
    bool platformIOInstall();
    QStringList platformIOListBoards();
    bool showPlatformIOConfigDialog(QString& outBoard, QString& outFramework, QString& outPort, QString& outSpeed);
    bool isMicrocontrollerConfigured();
    QJsonArray suggestPinsForBoard(const QString& board);
    // AI pin suggestion removed
    void logMessage(const QString& message, const QString& type = "INFO");
    bool saveProjectToFile(const QString& filePath);
    bool loadProjectFromFile(const QString& filePath);
};
