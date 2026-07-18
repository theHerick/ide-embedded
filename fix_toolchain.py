import os

def fix():
    # Fix ToolchainManager.cpp
    with open("apps/ide/src/ToolchainManager.cpp", "r", encoding="utf-8") as f:
        lines = f.readlines()

    for i, line in enumerate(lines):
        lines[i] = line.replace("QMessageBox::question(this,", "QMessageBox::question(m_mainWindow,")
        lines[i] = line.replace("QMessageBox::information(this,", "QMessageBox::information(m_mainWindow,")
        lines[i] = line.replace("QMessageBox::critical(this,", "QMessageBox::critical(m_mainWindow,")
        lines[i] = line.replace("QMessageBox::warning(this,", "QMessageBox::warning(m_mainWindow,")
        lines[i] = line.replace("QDialog dialog(this)", "QDialog dialog(m_mainWindow)")
        if "connect(" in line and "QObject::connect" not in line and "//" not in line:
            lines[i] = line.replace("connect(", "QObject::connect(")
        
    with open("apps/ide/src/ToolchainManager.cpp", "w", encoding="utf-8") as f:
        f.writelines(lines)

    # Fix MainWindow.cpp
    with open("apps/ide/src/MainWindow.cpp", "r", encoding="utf-8") as f:
        lines = f.readlines()

    for i, line in enumerate(lines):
        if "preparePlatformIOProject(" in line and "ToolchainManager" not in line:
            lines[i] = line.replace("preparePlatformIOProject(", "ToolchainManager(this).preparePlatformIOProject(")
        if "getPlatformIOCommand()" in line and "ToolchainManager" not in line:
            lines[i] = line.replace("getPlatformIOCommand()", "ToolchainManager(this).getPlatformIOCommand()")

    with open("apps/ide/src/MainWindow.cpp", "w", encoding="utf-8") as f:
        f.writelines(lines)

if __name__ == '__main__':
    fix()
