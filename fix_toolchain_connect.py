import os

def fix():
    with open("apps/ide/src/ToolchainManager.cpp", "r", encoding="utf-8") as f:
        lines = f.readlines()

    for i, line in enumerate(lines):
        if "QObject::connect(" in line and "this," in line:
            lines[i] = line.replace("this,", "m_mainWindow,")

    with open("apps/ide/src/ToolchainManager.cpp", "w", encoding="utf-8") as f:
        f.writelines(lines)

if __name__ == '__main__':
    fix()
