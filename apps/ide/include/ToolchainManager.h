#pragma once
#include <QString>
#include <QStringList>
#include <QJsonArray>

class MainWindow;

class ToolchainManager {
public:
    explicit ToolchainManager(MainWindow* mainWindow);

    void checkAndInstallToolchain();
    void checkPythonAsync();
    
    void preparePlatformIOProject(bool forNativeSimulation = false);
    bool platformIOBuild();
    bool platformIOUpload();
    bool platformIOIsInstalled();
    QString getPlatformIOCommand();
    bool platformIOInstall();
    QStringList platformIOListBoards();
    bool showPlatformIOConfigDialog(QString& outBoard, QString& outFramework, QString& outPort, QString& outSpeed);
    bool isMicrocontrollerConfigured();
    void parseResourceUsage(const QString& buildOutput);
    void platformIOConfigTriggered();
    QJsonArray suggestPinsForBoard(const QString& board);

private:
    MainWindow* m_mainWindow;
};
