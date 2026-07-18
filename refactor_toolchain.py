import os

def extract_methods(filepath, methods_to_extract):
    with open(filepath, "r", encoding="utf-8") as f:
        lines = f.readlines()
        
    extracted_code = []
    remaining_lines = []
    
    i = 0
    while i < len(lines):
        line = lines[i]
        
        # Check if the current line matches any of the method signatures
        matched_method = None
        for method in methods_to_extract:
            if line.startswith(f"void MainWindow::{method}(") or line.startswith(f"bool MainWindow::{method}(") or line.startswith(f"QString MainWindow::{method}(") or line.startswith(f"QStringList MainWindow::{method}(") or line.startswith(f"QJsonArray MainWindow::{method}("):
                matched_method = method
                break
                
        if matched_method:
            brace_count = 0
            method_lines = []
            started = False
            
            while i < len(lines):
                curr_line = lines[i]
                method_lines.append(curr_line.replace("MainWindow::", "ToolchainManager::"))
                
                if "{" in curr_line:
                    started = True
                    brace_count += curr_line.count("{")
                if "}" in curr_line:
                    brace_count -= curr_line.count("}")
                    
                i += 1
                
                if started and brace_count == 0:
                    break
                    
            extracted_code.extend(method_lines)
            extracted_code.append("\n")
            continue # Skip the i increment at the end of the outer loop
            
        else:
            remaining_lines.append(line)
            i += 1
            
    return remaining_lines, extracted_code

def main():
    mw_cpp_path = "apps/ide/src/MainWindow.cpp"
    methods = [
        "checkAndInstallToolchain",
        "checkPythonAsync",
        "parseResourceUsage",
        "preparePlatformIOProject",
        "platformIOBuild",
        "platformIOUpload",
        "platformIOIsInstalled",
        "getPlatformIOCommand",
        "platformIOListBoards",
        "platformIOInstall",
        "isMicrocontrollerConfigured",
        "showPlatformIOConfigDialog",
        "suggestPinsForBoard",
        "platformIOConfigTriggered"
    ]
    
    # Also extract from header
    mw_h_path = "apps/ide/include/MainWindow.h"
    with open(mw_h_path, "r", encoding="utf-8") as f:
        h_lines = f.readlines()
        
    new_h_lines = []
    for line in h_lines:
        skip = False
        for m in methods:
            # check signature presence precisely
            if " " + m + "(" in line:
                skip = True
                break
        if not skip:
            new_h_lines.append(line)
            
    # Add friend class ToolchainManager
    for i, line in enumerate(new_h_lines):
        if "friend class ProjectManager;" in line:
            new_h_lines.insert(i + 1, "    friend class ToolchainManager;\n")
            break
            
    with open(mw_h_path, "w", encoding="utf-8") as f:
        f.writelines(new_h_lines)
        
    remaining_cpp, extracted_cpp = extract_methods(mw_cpp_path, methods)
    
    # Add include in MainWindow.cpp
    for i, line in enumerate(remaining_cpp):
        if '#include "ProjectManager.h"' in line:
            remaining_cpp.insert(i + 1, '#include "ToolchainManager.h"\n')
            break
            
    # Replace usages in MainWindow.cpp to use ToolchainManager(this).xxx()
    for i, line in enumerate(remaining_cpp):
        if "&MainWindow::platformIOUpload" in line:
            remaining_cpp[i] = line.replace("&MainWindow::platformIOUpload", "[this]() { ToolchainManager(this).platformIOUpload(); }")
        elif "&MainWindow::platformIOConfigTriggered" in line:
            remaining_cpp[i] = line.replace("&MainWindow::platformIOConfigTriggered", "[this]() { ToolchainManager(this).platformIOConfigTriggered(); }")
        elif "&MainWindow::checkAndInstallToolchain" in line:
            remaining_cpp[i] = line.replace("&MainWindow::checkAndInstallToolchain", "[this]() { ToolchainManager(this).checkAndInstallToolchain(); }")
        elif "&MainWindow::checkPythonAsync" in line:
            remaining_cpp[i] = line.replace("&MainWindow::checkPythonAsync", "[this]() { ToolchainManager(this).checkPythonAsync(); }")
            
        # Replace direct calls with simple substring replacements safely
        if "isMicrocontrollerConfigured()" in line and "ToolchainManager" not in line:
            remaining_cpp[i] = remaining_cpp[i].replace("isMicrocontrollerConfigured()", "ToolchainManager(this).isMicrocontrollerConfigured()")
        if "showPlatformIOConfigDialog(" in line and "ToolchainManager" not in line:
            remaining_cpp[i] = remaining_cpp[i].replace("showPlatformIOConfigDialog(", "ToolchainManager(this).showPlatformIOConfigDialog(")
        if "platformIOIsInstalled()" in line and "ToolchainManager" not in line:
            remaining_cpp[i] = remaining_cpp[i].replace("platformIOIsInstalled()", "ToolchainManager(this).platformIOIsInstalled()")
        if "platformIOBuild()" in line and "ToolchainManager" not in line:
            remaining_cpp[i] = remaining_cpp[i].replace("platformIOBuild()", "ToolchainManager(this).platformIOBuild()")
    
    with open(mw_cpp_path, "w", encoding="utf-8") as f:
        f.writelines(remaining_cpp)
        
    # Generate ToolchainManager.h
    tm_h_content = """#pragma once
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
"""
    with open("apps/ide/include/ToolchainManager.h", "w", encoding="utf-8") as f:
        f.write(tm_h_content)
        
    # Generate ToolchainManager.cpp
    # Re-use the includes from MainWindow.cpp
    includes = remaining_cpp[0:70]
    
    tm_cpp_lines = []
    tm_cpp_lines.append('#include "ToolchainManager.h"\n')
    tm_cpp_lines.extend(includes)
    tm_cpp_lines.append("\n// --- HACK MACROS FOR DECOUPLING ---\n")
    tm_cpp_lines.append("#define m_compilerConsole m_mainWindow->m_compilerConsole\n")
    tm_cpp_lines.append("#define m_ramProgressBar m_mainWindow->m_ramProgressBar\n")
    tm_cpp_lines.append("#define m_flashProgressBar m_mainWindow->m_flashProgressBar\n")
    tm_cpp_lines.append("#define m_compiledCode m_mainWindow->m_compiledCode\n")
    tm_cpp_lines.append("#define m_currentProjectPath m_mainWindow->m_currentProjectPath\n")
    tm_cpp_lines.append("#define logMessage m_mainWindow->logMessage\n")
    tm_cpp_lines.append("#define statusBar() m_mainWindow->statusBar()\n")
    tm_cpp_lines.append("#define m_scene m_mainWindow->m_scene\n")
    tm_cpp_lines.append("#define m_buildAction m_mainWindow->m_buildAction\n")
    tm_cpp_lines.append("#define m_playAction m_mainWindow->m_playAction\n")
    tm_cpp_lines.append("#define updatePlayActionState() m_mainWindow->updatePlayActionState()\n")
    tm_cpp_lines.append("\nToolchainManager::ToolchainManager(MainWindow* mainWindow) : m_mainWindow(mainWindow) {}\n\n")
    
    tm_cpp_lines.extend(extracted_cpp)
    
    with open("apps/ide/src/ToolchainManager.cpp", "w", encoding="utf-8") as f:
        f.writelines(tm_cpp_lines)
        
    # Update CMakeLists.txt
    with open("apps/ide/CMakeLists.txt", "r", encoding="utf-8") as f:
        cmake_data = f.read()
    
    if "src/ToolchainManager.cpp" not in cmake_data:
        cmake_data = cmake_data.replace("src/ProjectManager.cpp", "src/ProjectManager.cpp\\n    src/ToolchainManager.cpp")
    
    with open("apps/ide/CMakeLists.txt", "w", encoding="utf-8") as f:
        f.write(cmake_data)

if __name__ == '__main__':
    main()
