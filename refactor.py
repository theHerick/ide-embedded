import os

def refactor():
    with open("apps/ide/src/MainWindow.cpp", "r", encoding="utf-8") as f:
        lines = f.readlines()

    includes = lines[0:67]
    serializers = lines[143:375] # Lines 144 to 375
    save_load = lines[2149:2329] # Lines 2150 to 2329

    # Remove the blocks from MainWindow in reverse order to keep indices valid
    del lines[2149:2329]
    del lines[143:375]
    
    # Insert ProjectManager include
    lines.insert(67, '#include "ProjectManager.h"\n')

    # Replace usages in MainWindow
    for i, line in enumerate(lines):
        if "if (saveProjectToFile(m_currentProjectPath)) {" in line:
            lines[i] = line.replace("saveProjectToFile(m_currentProjectPath)", "ProjectManager(this).saveProjectToFile(m_currentProjectPath)")
        elif "if (saveProjectToFile(filePath)) {" in line:
            lines[i] = line.replace("saveProjectToFile(filePath)", "ProjectManager(this).saveProjectToFile(filePath)")
        elif "if (!loadProjectFromFile(filePath)) {" in line:
            lines[i] = line.replace("loadProjectFromFile(filePath)", "ProjectManager(this).loadProjectFromFile(filePath)")
        elif "if (!saveProjectToFile(exportPath)) {" in line:
            lines[i] = line.replace("saveProjectToFile(exportPath)", "ProjectManager(this).saveProjectToFile(exportPath)")
        # Remove declarations if any left
        elif "bool MainWindow::saveProjectToFile(" in line:
            print("Warning: saveProjectToFile still in file!")
            
    with open("apps/ide/src/MainWindow.cpp", "w", encoding="utf-8") as f:
        f.writelines(lines)

    # Build ProjectManager.cpp
    pm_lines = []
    pm_lines.append('#include "ProjectManager.h"\n')
    pm_lines.extend(includes)
    
    pm_lines.append("\n// --- HACK MACROS FOR DECOUPLING ---\n")
    pm_lines.append("#define m_scene m_mainWindow->m_scene\n")
    pm_lines.append("#define m_blockEditor m_mainWindow->m_blockEditor\n")
    pm_lines.append("#define m_webPageData m_mainWindow->m_webPageData\n")
    pm_lines.append("#define m_simulator m_mainWindow->m_simulator\n")
    pm_lines.append("#define m_currentProjectPath m_mainWindow->m_currentProjectPath\n")
    pm_lines.append("#define loadToolboxItems() m_mainWindow->loadToolboxItems()\n")
    pm_lines.append("#define compileCode() m_mainWindow->compileCode()\n")
    pm_lines.append("#define statusBar() m_mainWindow->statusBar()\n")
    pm_lines.append("#define logMessage m_mainWindow->logMessage\n")
    pm_lines.append("\n")

    pm_lines.append("ProjectManager::ProjectManager(MainWindow* mainWindow) : m_mainWindow(mainWindow) {}\n\n")

    pm_lines.extend(serializers)
    
    for line in save_load:
        pm_lines.append(line.replace("bool MainWindow::", "bool ProjectManager::"))

    with open("apps/ide/src/ProjectManager.cpp", "w", encoding="utf-8") as f:
        f.writelines(pm_lines)

    # Update CMakeLists.txt
    with open("apps/ide/CMakeLists.txt", "r", encoding="utf-8") as f:
        cmake_data = f.read()
    
    if "src/ProjectManager.cpp" not in cmake_data:
        cmake_data = cmake_data.replace("src/MainWindow.cpp", "src/MainWindow.cpp\n    src/ProjectManager.cpp")
    
    with open("apps/ide/CMakeLists.txt", "w", encoding="utf-8") as f:
        f.write(cmake_data)

if __name__ == '__main__':
    refactor()
