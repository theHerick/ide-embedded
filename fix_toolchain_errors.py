import re

def fix():
    with open("apps/ide/src/ToolchainManager.cpp", "r", encoding="utf-8") as f:
        code = f.read()

    # Fix disconnect typo
    code = code.replace("disQObject::connect(", "QObject::disconnect(")

    # Fix lambda captures to avoid `m_mainWindow` missing scope
    code = code.replace("[this, pyProc, pyTimer, onFailed]", "[=]")
    code = code.replace("[pyProc, pyTimer, onFailed]", "[=]")
    code = code.replace("[this, installProc]", "[=]")
    code = code.replace("[boardEdit, searchFeedbackLabel, this]", "[=]")
    
    # Fix QMessageBox and other UI parents
    code = re.sub(r'QMessageBox::(\w+)\s*\(\s*this\s*,', r'QMessageBox::\1(m_mainWindow,', code)
    code = re.sub(r'QDialog\s+(\w+)\s*\(\s*this\s*\)', r'QDialog \1(m_mainWindow)', code)
    code = re.sub(r'QInputDialog::(\w+)\s*\(\s*this\s*,', r'QInputDialog::\1(m_mainWindow,', code)

    # Ensure compileCode() macro exists
    if "#define compileCode() m_mainWindow->compileCode()" not in code:
        code = code.replace("#define m_compiledCode m_mainWindow->m_compiledCode\n",
                            "#define m_compiledCode m_mainWindow->m_compiledCode\n#define compileCode() m_mainWindow->compileCode()\n")

    with open("apps/ide/src/ToolchainManager.cpp", "w", encoding="utf-8") as f:
        f.write(code)

if __name__ == '__main__':
    fix()
