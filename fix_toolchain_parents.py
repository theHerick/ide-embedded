import re

def fix():
    with open("apps/ide/src/ToolchainManager.cpp", "r", encoding="utf-8") as f:
        code = f.read()

    # Replace new QProcess(this) and new QTimer(this)
    code = re.sub(r'new\s+QProcess\s*\(\s*this\s*\)', 'new QProcess(m_mainWindow)', code)
    code = re.sub(r'new\s+QTimer\s*\(\s*this\s*\)', 'new QTimer(m_mainWindow)', code)
    code = re.sub(r'new\s+QTcpSocket\s*\(\s*this\s*\)', 'new QTcpSocket(m_mainWindow)', code)
    code = re.sub(r'new\s+QNetworkAccessManager\s*\(\s*this\s*\)', 'new QNetworkAccessManager(m_mainWindow)', code)

    with open("apps/ide/src/ToolchainManager.cpp", "w", encoding="utf-8") as f:
        f.write(code)

if __name__ == '__main__':
    fix()
