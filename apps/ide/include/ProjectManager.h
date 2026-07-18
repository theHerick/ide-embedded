#pragma once
#include <QString>

class MainWindow;

class ProjectManager {
public:
    explicit ProjectManager(MainWindow* mainWindow);
    
    bool saveProjectToFile(const QString& filePath);
    bool loadProjectFromFile(const QString& filePath);

private:
    MainWindow* m_mainWindow;
};
