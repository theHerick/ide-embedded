import re

def fix():
    with open("apps/ide/src/ToolchainManager.cpp", "r", encoding="utf-8") as f:
        code = f.read()

    # Replace any [m_mainWindow...] capture with [this...]
    code = re.sub(r'\[\s*m_mainWindow', '[this', code)

    # Make sure we don't have [this, this...
    code = code.replace("[this, this", "[this")

    with open("apps/ide/src/ToolchainManager.cpp", "w", encoding="utf-8") as f:
        f.write(code)

if __name__ == '__main__':
    fix()
