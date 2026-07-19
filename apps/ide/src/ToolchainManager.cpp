#include "ToolchainManager.h"
#include "MainWindow.h"
#include "CodeGenerator.h"
#include "PcbExporter.h"
#include "CustomComponent.h"
#include "ComponentCreatorDialog.h"
#include "WebPageEditorDialog.h"
#include "ComponentItem.h"
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QTime>
#include <QTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include "UndoCommands.h"
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QHeaderView>
#include <QScrollBar>
#include <QDialog>
#include <QScrollArea>
#ifdef IDE_EMBEDDED_HAS_QT_PDF
#include <QPdfDocument>
#endif
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>

#include <QComboBox>
#include <QCheckBox>
#include <QProcess>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QStandardPaths>
#include <QSerialPortInfo>
#include <QSerialPort>
#include <QTableWidgetItem>
#include <QSlider>
#include <QSpinBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QSettings>
#include <QHash>
#include <QDir>
#include <QRegularExpression>
#include <QImage>
#include <QPixmap>
#include <QScrollArea>
#include <QTextBrowser>
#include <QPainter>
#include <QMouseEvent>
#include <QScopeGuard>

#include "ProjectManager.h"
#include "ToolchainManager.h"
// --- Measurement Tool for PCB Preview ---

// --- HACK MACROS FOR DECOUPLING ---
#define m_compilerConsole m_mainWindow->m_compilerConsole
#define m_ramProgressBar m_mainWindow->m_ramProgressBar
#define m_flashProgressBar m_mainWindow->m_flashProgressBar
#define m_compiledCode m_mainWindow->m_compiledCode
#define compileCode() m_mainWindow->compileCode()
#define m_currentProjectPath m_mainWindow->m_currentProjectPath
#define logMessage m_mainWindow->logMessage
#define statusBar() m_mainWindow->statusBar()
#define m_scene m_mainWindow->m_scene
#define m_buildAction m_mainWindow->m_buildAction
#define m_playAction m_mainWindow->m_playAction
#define updatePlayActionState() m_mainWindow->updatePlayActionState()

ToolchainManager::ToolchainManager(MainWindow* mainWindow) : m_mainWindow(mainWindow) {}

void ToolchainManager::checkAndInstallToolchain() {
    logMessage("Verificando ferramentas de hardware (Python/PlatformIO)...", "SYSTEM");
    
    QString pioCmd = getPlatformIOCommand();
    if (!pioCmd.isEmpty()) {
        logMessage(QString("PlatformIO detectado com sucesso! Caminho: %1").arg(pioCmd), "SUCCESS");
    } else {
        checkPythonAsync();
    }
}

void ToolchainManager::checkPythonAsync() {
    QProcess* pyProc = new QProcess(m_mainWindow);
    
    auto onFailed = [this, pyProc]() {
        pyProc->deleteLater();
        
        auto res = QMessageBox::question(m_mainWindow, "Ferramentas Faltando", 
            "O Python (necessário para compilação e gravação de hardware) não foi detectado.\n\n"
            "Deseja que a IDE tente instalar o Python 3.12 e o PlatformIO de forma 100% automática e silenciosa agora?",
            QMessageBox::Yes | QMessageBox::No);
            
        if (res == QMessageBox::Yes) {
            logMessage("Iniciando instalação silenciosa do Python via winget...", "WARNING");
            statusBar()->showMessage("Instalando Python automaticamente... Isso pode levar alguns minutos.");
            
            QProcess* wingetProc = new QProcess(m_mainWindow);
            QObject::connect(wingetProc, &QProcess::finished, m_mainWindow, [this, wingetProc](int exitCode) {
                if (exitCode == 0) {
                    logMessage("Python instalado com sucesso via winget! Iniciando instalação do PlatformIO...", "SUCCESS");
                    statusBar()->showMessage("Python instalado! Iniciando instalação do PlatformIO...");
                    
                    QProcess* installPio = new QProcess(m_mainWindow);
                    QObject::connect(installPio, &QProcess::finished, m_mainWindow, [this, installPio](int pioExit) {
                        if (pioExit == 0) {
                            logMessage("PlatformIO instalado com sucesso! Reinicie a IDE para aplicar.", "SUCCESS");
                            QMessageBox::information(m_mainWindow, "Sucesso", "Python 3.12 e PlatformIO foram instalados automaticamente com sucesso! Por favor, reinicie a IDE para habilitar as funções de gravação.");
                        } else {
                            logMessage("Falha ao instalar PlatformIO após instalar Python. Reinicie a IDE e tente rodar 'pip install platformio' no terminal.", "ERROR");
                        }
                        installPio->deleteLater();
                    });
                    
                    QString pyPath = QDir::homePath() + "/AppData/Local/Programs/Python";
                    QDir pyDir(pyPath);
                    QString pyExe = "python";
                    if (pyDir.exists()) {
                        QStringList entries = pyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                        for (const QString& entry : entries) {
                            if (entry.toLower().contains("python")) {
                                QString testExe = pyDir.absoluteFilePath(entry + "/python.exe");
                                if (QFile::exists(testExe)) {
                                    pyExe = testExe;
                                    break;
                                }
                            }
                        }
                    }
                    installPio->start(pyExe, {"-m", "pip", "install", "-U", "platformio"});
                } else {
                    logMessage("Falha na instalação automática do Python via winget.", "ERROR");
                    auto openWeb = QMessageBox::question(m_mainWindow, "Falha na Instalação", 
                        "Não foi possível instalar o Python automaticamente via winget.\n\n"
                        "Deseja abrir a página oficial de downloads do Python para fazer a instalação manual?",
                        QMessageBox::Yes | QMessageBox::No);
                    if (openWeb == QMessageBox::Yes) {
                        QDesktopServices::openUrl(QUrl("https://www.python.org/downloads/"));
                    }
                }
                wingetProc->deleteLater();
            });
            
            wingetProc->start("winget", {"install", "--id", "Python.Python.3.12", "--silent", "--accept-source-agreements", "--accept-package-agreements"});
        }
    };

    QTimer* pyTimer = new QTimer(m_mainWindow);
    pyTimer->setSingleShot(true);
    
    QObject::connect(pyTimer, &QTimer::timeout, m_mainWindow, [=]() {
        pyTimer->deleteLater();
        if (pyProc->state() == QProcess::Running) {
            pyProc->kill();
            onFailed();
        }
    });

    QObject::connect(pyProc, &QProcess::finished, m_mainWindow, [this, pyProc, pyTimer, onFailed](int exitCode, QProcess::ExitStatus exitStatus) {
        pyTimer->stop();
        pyTimer->deleteLater();
        pyProc->deleteLater();

        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            // Python existe, mas PIO não. Oferece instalação automática.
            auto res = QMessageBox::question(m_mainWindow, "Instalar PlatformIO", 
                "O motor de Flash (PlatformIO) não foi encontrado, mas o Python está presente.\n\n"
                "Deseja que a IDE tente instalar o PlatformIO automaticamente agora?",
                QMessageBox::Yes | QMessageBox::No);

            if (res == QMessageBox::Yes) {
                logMessage("Iniciando instalação do PlatformIO via pip...", "WARNING");
                statusBar()->showMessage("Instalando PlatformIO... Isso pode levar alguns minutos.");
                
                QProcess* installProc = new QProcess(m_mainWindow);
                QObject::connect(installProc, &QProcess::finished, m_mainWindow, [this, installProc](int exitCode) {
                    if (exitCode == 0) {
                        logMessage("PlatformIO instalado com sucesso! Reinicie a IDE para aplicar.", "SUCCESS");
                        QMessageBox::information(m_mainWindow, "Sucesso", "PlatformIO instalado! Por favor, reinicie a IDE para habilitar o Flash.");
                    } else {
                        logMessage("Falha na instalação automática. Tente: 'pip install platformio' no terminal.", "ERROR");
                    }
                    installProc->deleteLater();
                });
                
                installProc->start("python", {"-m", "pip", "install", "-U", "platformio"});
            }
        } else {
            onFailed();
        }
    });

    QObject::connect(pyProc, &QProcess::errorOccurred, m_mainWindow, [=](QProcess::ProcessError err) {
        Q_UNUSED(err);
        pyTimer->stop();
        pyTimer->deleteLater();
        QObject::disconnect(pyProc, &QProcess::finished, nullptr, nullptr);
        pyProc->deleteLater();
        onFailed();
    });

    pyProc->start("python", {"--version"});
    pyTimer->start(2000); // 2 segundos de timeout
}

void ToolchainManager::preparePlatformIOProject(bool forNativeSimulation) {
    // Ensure generated code is up to date
    compileCode();

    QDir buildDir(qApp->applicationDirPath());
    // project build directory: <appdir>/pio_project
    QString pioPath = buildDir.filePath("pio_project");
    QDir pioDir(pioPath);
    if (!pioDir.exists()) pioDir.mkpath(".");

    if (forNativeSimulation) {
        // Write platformio.ini for native
        QFile iniFile(pioDir.filePath("platformio.ini"));
        if (iniFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QString ini = "[env:native]\nplatform = native\nbuild_flags = -std=c++17\n";
            iniFile.write(ini.toUtf8());
            iniFile.close();
        }

        // Write src/arduino_sim.h
        QDir srcDir(pioDir.filePath("src"));
        if (!srcDir.exists()) srcDir.mkpath(".");
        QFile mockFile(srcDir.filePath("arduino_sim.h"));
        if (mockFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QString mockCode = R"(#pragma once
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

static std::map<int, int> g_pinStates;
static std::mutex g_pinMutex;

static void pinMode(int pin, int mode) {}
static void digitalWrite(int pin, int value) {
    std::cout << "PIN:" << pin << ":" << (value ? "HIGH" : "LOW") << std::endl;
}
static int digitalRead(int pin) {
    std::lock_guard<std::mutex> lock(g_pinMutex);
    return g_pinStates[pin];
}
static int analogRead(int pin) { return 0; }
static void analogWrite(int pin, int value) {
    std::cout << "PWM:" << pin << ":" << value << std::endl;
}
static unsigned long millis() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}
static void delay(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
static void tone(int pin, unsigned int frequency, unsigned long duration = 0) {
    std::cout << "TONE:" << pin << ":" << frequency << std::endl;
}
static void noTone(int pin) {
    std::cout << "NOTONE:" << pin << std::endl;
}

struct SerialMock {
    void begin(long baud) {}
    void print(const std::string& s) { std::cout << "SERIAL:" << s; }
    void print(int n) { std::cout << "SERIAL:" << n; }
    void println(const std::string& s) { std::cout << "SERIAL:" << s << std::endl; }
    void println(int n) { std::cout << "SERIAL:" << n << std::endl; }
    void println() { std::cout << "SERIAL:" << std::endl; }
};
static SerialMock Serial;

class String : public std::string {
public:
    String() : std::string() {}
    String(const char* s) : std::string(s) {}
    String(int n) : std::string(std::to_string(n)) {}
    int toInt() const { return std::stoi(*this); }
    float toFloat() const { return std::stof(*this); }
};

static void _simInputThread() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.rfind("SET:", 0) == 0) {
            size_t firstColon = 3;
            size_t secondColon = line.find(':', 4);
            if (secondColon != std::string::npos) {
                try {
                    int pin = std::stoi(line.substr(4, secondColon - 4));
                    int val = std::stoi(line.substr(secondColon + 1));
                    std::lock_guard<std::mutex> lock(g_pinMutex);
                    g_pinStates[pin] = val;
                } catch(...) {}
            }
        }
    }
}
)";
            mockFile.write(mockCode.toUtf8());
            mockFile.close();
        }

        // Write src/main.cpp
        QFile cppFile(srcDir.filePath("main.cpp"));
        if (cppFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QString code = m_compiledCode.isEmpty() ? "// Nenhum código gerado\nint main() { return 0; }" : m_compiledCode;
            
            // Replace <Arduino.h> with "arduino_sim.h"
            if (code.contains("<Arduino.h>")) {
                code.replace("<Arduino.h>", "\"arduino_sim.h\"");
            } else {
                code = "#include \"arduino_sim.h\"\n" + code;
            }

            // Append main() entry point for native
            code += "\n\nint main() {\n  std::thread t(_simInputThread);\n  t.detach();\n  setup();\n  while(true) {\n    loop();\n    delay(10);\n  }\n  return 0;\n}\n";

            cppFile.write(code.toUtf8());
            cppFile.close();
        }

        QFile oldIno(srcDir.filePath("main.ino"));
        if (oldIno.exists()) oldIno.remove();

        return; // Early return for native
    }

    // Default configuration values for hardware compile
    QString board = "esp32dev";
    QString framework = "arduino";
    QString uploadPort = "Auto-Detect";
    QString uploadSpeed = "Auto";
    QString platform = "espressif32";

    // 1. Get active microcontroller config
    ComponentItem* mcu = nullptr;
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "esp32" || comp->componentType() == "esp8266" || comp->name().contains("esp", Qt::CaseInsensitive)) {
            mcu = comp;
            break;
        }
    }

    if (mcu) {
        QVariant existing = mcu->property("microcontrollerConfig");
        if (existing.isValid() && existing.canConvert<QString>()) {
            QJsonDocument d = QJsonDocument::fromJson(existing.toString().toUtf8());
            if (d.isObject()) {
                QJsonObject o = d.object();
                if (o.contains("board")) board = o["board"].toString();
                if (o.contains("core")) framework = o["core"].toString();
                if (o.contains("upload_port")) uploadPort = o["upload_port"].toString();
                if (o.contains("upload_speed")) uploadSpeed = o["upload_speed"].toString();
            }
        }
    }

    // Map internal/legacy board names to recognized PlatformIO board IDs
    if (board == "esp32-c3-mini" || board == "esp32-c3-wroom-02") {
        board = "esp32-c3-devkitm-1";
    }

    // Determine platform based on board name
    if (board.contains("esp32") || board.contains("wrover") || board.contains("nodemcu-32")) {
        platform = "espressif32";
    } else if (board.contains("esp8266") || board.contains("nodemcu") || board.contains("d1_mini")) {
        platform = "espressif8266";
    } else {
        if (mcu && mcu->componentType() == "esp8266") {
            platform = "espressif8266";
        } else {
            platform = "espressif32";
        }
    }

    // Write platformio.ini
    QFile iniFile(pioDir.filePath("platformio.ini"));
    if (iniFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString ini = QString("[env:%1]\nplatform = %2\nboard = %3\nframework = %4\n")
                      .arg(board, platform, board, framework);
        
        // Otimizações para a ESP: 
        // -Os: foca em reduzir o tamanho do binário (enxuto)
        // LTO e garbage collection: remove irredundâncias
        // Isso diminui o uso de RAM e reduz os blocos gravados no Flash, poupando desgaste da memória.
        ini += "build_unflags = -Os\n";
        ini += "build_flags = -Oz -ffunction-sections -fdata-sections -DCORE_DEBUG_LEVEL=0\n";

        if (uploadPort != "Auto-Detect" && !uploadPort.trimmed().isEmpty()) {
            ini += QString("upload_port = %1\n").arg(uploadPort);
            ini += QString("monitor_port = %1\n").arg(uploadPort);
        }
        if (uploadSpeed != "Auto" && !uploadSpeed.trimmed().isEmpty()) {
            ini += QString("upload_speed = %1\n").arg(uploadSpeed);
            ini += QString("monitor_speed = %1\n").arg(uploadSpeed);
        }
        iniFile.write(ini.toUtf8());
        iniFile.close();
    }

    // Write src/main.cpp
    QDir srcDir(pioDir.filePath("src"));
    if (!srcDir.exists()) srcDir.mkpath(".");
    QFile cppFile(srcDir.filePath("main.cpp"));
    if (cppFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString code = m_compiledCode.isEmpty() ? "// Nenhum código gerado" : m_compiledCode;
        if (framework == "arduino" && !code.contains("Arduino.h")) {
            code = "#include <Arduino.h>\n" + code;
        }
        // Limpar "arduino_sim.h" caso tenha ficado na memória do m_compiledCode gerado nativo?
        // m_compiledCode é gerado pelo CodeGenerator, que sempre gera padrão.
        cppFile.write(code.toUtf8());
        cppFile.close();
    }

    // Delete old src/main.ino to avoid duplicate symbols compile error
    QFile oldIno(srcDir.filePath("main.ino"));
    if (oldIno.exists()) {
        oldIno.remove();
    }
}

bool ToolchainManager::platformIOBuild() {
    if (!platformIOIsInstalled()) {
        if (!platformIOInstall()) return false;
    }
    preparePlatformIOProject();
    QDir buildDir(qApp->applicationDirPath());
    QString pioPath = buildDir.filePath("pio_project");

    QProcess p;
    QString pioCmd = getPlatformIOCommand();
    if (pioCmd.isEmpty()) pioCmd = "platformio";
    p.setProgram(pioCmd);
    p.setArguments({"run"});
    p.setWorkingDirectory(pioPath);
    logMessage("Iniciando compilação de validação do PlatformIO...", "INFO");
    p.start();
    
    QString outBuffer;
    while (p.state() == QProcess::Running) {
        QCoreApplication::processEvents();
        if (p.waitForReadyRead(100)) {
            QByteArray outChunk = p.readAllStandardOutput();
            if (!outChunk.isEmpty()) {
                QString outStr = QString::fromUtf8(outChunk);
                m_compilerConsole->appendPlainText(outStr);
                m_compilerConsole->verticalScrollBar()->setValue(m_compilerConsole->verticalScrollBar()->maximum());
                
                outBuffer += outStr;
                int pos;
                while ((pos = outBuffer.indexOf('\n')) != -1) {
                    QString line = outBuffer.left(pos);
                    outBuffer.remove(0, pos + 1);
                    parseResourceUsage(line);
                }
            }
            QByteArray errChunk = p.readAllStandardError();
            if (!errChunk.isEmpty()) {
                m_compilerConsole->appendPlainText(QString::fromUtf8(errChunk));
                m_compilerConsole->verticalScrollBar()->setValue(m_compilerConsole->verticalScrollBar()->maximum());
            }
        }
    }
    p.waitForFinished();

    // Read remaining
    QByteArray outChunk = p.readAllStandardOutput();
    if (!outChunk.isEmpty()) {
        QString outStr = QString::fromUtf8(outChunk);
        m_compilerConsole->appendPlainText(outStr);
        m_compilerConsole->verticalScrollBar()->setValue(m_compilerConsole->verticalScrollBar()->maximum());
        
        outBuffer += outStr;
        int pos;
        while ((pos = outBuffer.indexOf('\n')) != -1) {
            QString line = outBuffer.left(pos);
            outBuffer.remove(0, pos + 1);
            parseResourceUsage(line);
        }
    }
    if (!outBuffer.isEmpty()) {
        parseResourceUsage(outBuffer);
    }
    
    QByteArray errChunk = p.readAllStandardError();
    if (!errChunk.isEmpty()) {
        m_compilerConsole->appendPlainText(QString::fromUtf8(errChunk));
        m_compilerConsole->verticalScrollBar()->setValue(m_compilerConsole->verticalScrollBar()->maximum());
    }

    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        logMessage("PlatformIO: Compilação realizada com sucesso!", "SUCCESS");
        return true;
    } else {
        logMessage(QString("PlatformIO: Falha na compilação. Código de saída: %1").arg(p.exitCode()), "ERROR");
        return false;
    }
}

bool ToolchainManager::platformIOUpload() {
    if (!platformIOIsInstalled()) {
        if (!platformIOInstall()) return false;
    }

    // Validação da configuração antes de continuar
    if (!isMicrocontrollerConfigured()) {
        auto resp = QMessageBox::question(m_mainWindow, "Configuração Necessária",
            "A placa ou framework do microcontrolador ainda não foram configurados.\nDeseja realizar essa configuração agora?",
            QMessageBox::Yes | QMessageBox::No);
        if (resp == QMessageBox::Yes) {
            QString b, f, p, s;
            if (!showPlatformIOConfigDialog(b, f, p, s)) {
                logMessage("Gravação cancelada pelo usuário.", "WARNING");
                return false;
            }
        } else {
            logMessage("Gravação cancelada: microcontrolador não configurado.", "ERROR");
            return false;
        }
    }

    preparePlatformIOProject();
    QDir buildDir(qApp->applicationDirPath());
    QString pioPath = buildDir.filePath("pio_project");

    QProcess p;
    QString pioCmd = getPlatformIOCommand();
    if (pioCmd.isEmpty()) pioCmd = "platformio";
    p.setProgram(pioCmd);
    p.setArguments({"run", "-t", "upload"});
    p.setWorkingDirectory(pioPath);
    logMessage("Iniciando gravação na placa (Upload via PlatformIO)...", "INFO");
    p.start();
    
    QString outBuffer;
    while (p.state() == QProcess::Running) {
        QCoreApplication::processEvents();
        if (p.waitForReadyRead(100)) {
            QByteArray outChunk = p.readAllStandardOutput();
            if (!outChunk.isEmpty()) {
                QString outStr = QString::fromUtf8(outChunk);
                m_compilerConsole->appendPlainText(outStr);
                m_compilerConsole->verticalScrollBar()->setValue(m_compilerConsole->verticalScrollBar()->maximum());
                
                outBuffer += outStr;
                int pos;
                while ((pos = outBuffer.indexOf('\n')) != -1) {
                    QString line = outBuffer.left(pos);
                    outBuffer.remove(0, pos + 1);
                    parseResourceUsage(line);
                }
            }
            QByteArray errChunk = p.readAllStandardError();
            if (!errChunk.isEmpty()) {
                m_compilerConsole->appendPlainText(QString::fromUtf8(errChunk));
                m_compilerConsole->verticalScrollBar()->setValue(m_compilerConsole->verticalScrollBar()->maximum());
            }
        }
    }
    p.waitForFinished();

    // Read remaining
    QByteArray outChunk = p.readAllStandardOutput();
    if (!outChunk.isEmpty()) {
        QString outStr = QString::fromUtf8(outChunk);
        m_compilerConsole->appendPlainText(outStr);
        m_compilerConsole->verticalScrollBar()->setValue(m_compilerConsole->verticalScrollBar()->maximum());
        
        outBuffer += outStr;
        int pos;
        while ((pos = outBuffer.indexOf('\n')) != -1) {
            QString line = outBuffer.left(pos);
            outBuffer.remove(0, pos + 1);
            parseResourceUsage(line);
        }
    }
    if (!outBuffer.isEmpty()) {
        parseResourceUsage(outBuffer);
    }
    
    QByteArray errChunk = p.readAllStandardError();
    if (!errChunk.isEmpty()) {
        m_compilerConsole->appendPlainText(QString::fromUtf8(errChunk));
        m_compilerConsole->verticalScrollBar()->setValue(m_compilerConsole->verticalScrollBar()->maximum());
    }

    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        logMessage("PlatformIO: Gravação na placa realizada com sucesso!", "SUCCESS");
        return true;
    } else {
        logMessage(QString("PlatformIO: Falha na gravação. Código de saída: %1").arg(p.exitCode()), "ERROR");
        return false;
    }
}

bool ToolchainManager::platformIOIsInstalled() {
    return !getPlatformIOCommand().isEmpty();
}

QString ToolchainManager::getPlatformIOCommand() {
    // 1. Test "pio" directly (system PATH)
    {
        QProcess p;
        p.setProgram("pio");
        p.setArguments({"--version"});
        p.start();
        if (p.waitForFinished(1000) && p.exitCode() == 0) {
            return "pio";
        }
    }
    // 2. Test "platformio" directly (system PATH)
    {
        QProcess p;
        p.setProgram("platformio");
        p.setArguments({"--version"});
        p.start();
        if (p.waitForFinished(1000) && p.exitCode() == 0) {
            return "platformio";
        }
    }

    // 3. Test Windows common paths
#ifdef Q_OS_WIN
    // 3a. VS Code PlatformIO Core
    QString pioVscode = QDir::homePath() + "/.platformio/penv/Scripts/pio.exe";
    if (QFile::exists(pioVscode)) {
        return QDir::toNativeSeparators(pioVscode);
    }
    QString platformioVscode = QDir::homePath() + "/.platformio/penv/Scripts/platformio.exe";
    if (QFile::exists(platformioVscode)) {
        return QDir::toNativeSeparators(platformioVscode);
    }

    // 3b. Local Python Scripts
    QString localPythonDir = QDir::homePath() + "/AppData/Local/Programs/Python";
    QDir dir(localPythonDir);
    if (dir.exists()) {
        QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& subdir : subdirs) {
            if (subdir.toLower().contains("python")) {
                QString path1 = dir.absoluteFilePath(subdir + "/Scripts/pio.exe");
                if (QFile::exists(path1)) return QDir::toNativeSeparators(path1);
                QString path2 = dir.absoluteFilePath(subdir + "/Scripts/platformio.exe");
                if (QFile::exists(path2)) return QDir::toNativeSeparators(path2);
            }
        }
    }

    // 3c. Roaming Python Scripts
    QString roamingPythonDir = QDir::homePath() + "/AppData/Roaming/Python";
    QDir rDir(roamingPythonDir);
    if (rDir.exists()) {
        QStringList subdirs = rDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& subdir : subdirs) {
            if (subdir.toLower().contains("python")) {
                QString path1 = rDir.absoluteFilePath(subdir + "/Scripts/pio.exe");
                if (QFile::exists(path1)) return QDir::toNativeSeparators(path1);
                QString path2 = rDir.absoluteFilePath(subdir + "/Scripts/platformio.exe");
                if (QFile::exists(path2)) return QDir::toNativeSeparators(path2);
            }
        }
    }
#endif

    return "";
}

QStringList ToolchainManager::platformIOListBoards() {
    QStringList res;
    QProcess p;
    QString pioCmd = getPlatformIOCommand();
    if (pioCmd.isEmpty()) pioCmd = "platformio";
    p.setProgram(pioCmd);
    p.setArguments({"boards", "--json-output"});
    p.start();
    if (!p.waitForFinished(8000)) return QStringList({"esp32dev"});
    QByteArray out = p.readAllStandardOutput();
    QJsonParseError err;
    QJsonDocument d = QJsonDocument::fromJson(out, &err);
    if (err.error == QJsonParseError::NoError && d.isObject()) {
        QJsonObject obj = d.object();
        // obj may contain platforms each with list of boards
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (it.value().isArray()) {
                for (const auto& b : it.value().toArray()) {
                    if (b.isObject()) {
                        QString id = b.toObject().value("id").toString();
                        if (!id.isEmpty() && !res.contains(id)) res.append(id);
                    }
                }
            }
        }
    }
    if (res.isEmpty()) res = QStringList({"esp32dev"});
    return res;
}

QJsonArray ToolchainManager::suggestPinsForBoard(const QString& board) {
    QString b = board.toLower();
    QJsonArray arr;
    if (b.contains("esp32")) {
        QList<QPair<QString,QString>> defaults = {
            {"VCC","3V3"}, {"GND","GND"}, {"TX0","TX0"}, {"RX0","RX0"}, {"SDA","SDA"}, {"SCL","SCL"},
            {"GPIO2","2"}, {"GPIO4","4"}, {"GPIO5","5"}
        };
        for (auto &p : defaults) {
            QJsonObject o; o["name"] = p.first; o["pin"] = p.second; o["role"] = "signal"; arr.append(o);
        }
    } else if (b.contains("esp8266")) {
        QList<QPair<QString,QString>> defaults = {{"VCC","3V3"},{"GND","GND"},{"TX0","TX0"},{"RX0","RX0"},{"D1","D1"},{"D2","D2"}};
        for (auto &p : defaults) { QJsonObject o; o["name"] = p.first; o["pin"] = p.second; o["role"] = "signal"; arr.append(o); }
    } else {
        // generic fallback
        QJsonObject o1; o1["name"] = "VCC"; o1["pin"] = "VCC"; o1["role"] = "power"; arr.append(o1);
        QJsonObject o2; o2["name"] = "GND"; o2["pin"] = "GND"; o2["role"] = "ground"; arr.append(o2);
    }
    return arr;
}

bool ToolchainManager::platformIOInstall() {
    auto resp = QMessageBox::question(m_mainWindow, "Instalar PlatformIO?", "PlatformIO não foi encontrado no sistema. Deseja instalar via pip agora?", QMessageBox::Yes | QMessageBox::No);
    if (resp != QMessageBox::Yes) return false;

    // Try python -m pip install -U platformio
    QProcess p;
    p.setProgram("python");
    p.setArguments({"-m", "pip", "install", "-U", "platformio"});
    p.start();
    logMessage("Instalando PlatformIO via pip...", "INFO");
    if (!p.waitForFinished(600000)) {
        logMessage("Instalação do PlatformIO excedeu o tempo limite.", "ERROR");
        return false;
    }
    QString out = p.readAllStandardOutput();
    QString err = p.readAllStandardError();
    if (!err.isEmpty()) logMessage(err, "ERROR");
    logMessage(out, "SUCCESS");
    // Verify
    return platformIOIsInstalled();
}

bool ToolchainManager::isMicrocontrollerConfigured() {
    ComponentItem* mcu = nullptr;
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "esp32" || comp->componentType() == "esp8266" || comp->name().contains("esp", Qt::CaseInsensitive)) {
            mcu = comp;
            break;
        }
    }
    if (!mcu) return true; // Se não tem microcontrolador na cena, não há o que configurar

    QVariant existing = mcu->property("microcontrollerConfig");
    if (!existing.isValid() || !existing.canConvert<QString>() || existing.toString().trimmed().isEmpty()) {
        return false;
    }

    QJsonDocument d = QJsonDocument::fromJson(existing.toString().toUtf8());
    if (!d.isObject()) return false;

    QJsonObject o = d.object();
    return o.contains("board") && !o["board"].toString().isEmpty() &&
           o.contains("core") && !o["core"].toString().isEmpty();
}

bool ToolchainManager::showPlatformIOConfigDialog(QString& outBoard, QString& outFramework, QString& outPort, QString& outSpeed) {
    ComponentItem* mcu = nullptr;
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "esp32" || comp->componentType() == "esp8266" || comp->name().contains("esp", Qt::CaseInsensitive)) {
            mcu = comp;
            break;
        }
    }

    QString currentBoard = "esp32dev";
    QString currentFramework = "arduino";
    QString currentPort = "Auto-Detect";
    QString currentSpeed = "Auto";

    if (mcu) {
        QVariant existing = mcu->property("microcontrollerConfig");
        if (existing.isValid() && existing.canConvert<QString>()) {
            QJsonDocument d = QJsonDocument::fromJson(existing.toString().toUtf8());
            if (d.isObject()) {
                QJsonObject o = d.object();
                if (o.contains("board")) currentBoard = o["board"].toString();
                if (o.contains("core")) currentFramework = o["core"].toString();
                if (o.contains("upload_port")) currentPort = o["upload_port"].toString();
                if (o.contains("upload_speed")) currentSpeed = o["upload_speed"].toString();
            }
        }
    }

    QDialog dialog(m_mainWindow);
    dialog.setWindowTitle("Configurações do PlatformIO");
    dialog.setMinimumWidth(400);
    dialog.setStyleSheet("background-color: #F8FAFC; color: #0F172A;");

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto* title = new QLabel("Selecione os parâmetros de Compilação e Gravação:", &dialog);
    title->setStyleSheet("font-weight: bold; font-size: 13px; color: #1E3A8A;");
    layout->addWidget(title);

    auto* formLayout = new QFormLayout();
    formLayout->setSpacing(10);

    // Board Selection
    auto* boardLayout = new QHBoxLayout();
    boardLayout->setSpacing(6);
    boardLayout->setContentsMargins(0, 0, 0, 0);

    auto* boardEdit = new QLineEdit(&dialog);
    boardEdit->setText(currentBoard);
    boardEdit->setPlaceholderText("Ex: esp32dev, nodemcuv2...");
    boardLayout->addWidget(boardEdit, 2);

    auto* searchBoardBtn = new QPushButton(&dialog);
    searchBoardBtn->setIcon(QIcon(":/icons/search.svg"));
    searchBoardBtn->setIconSize(QSize(18, 18));
    searchBoardBtn->setToolTip("Buscar placa no PlatformIO");
    searchBoardBtn->setFixedWidth(36);
    searchBoardBtn->setObjectName("cancel");
    boardLayout->addWidget(searchBoardBtn);

    formLayout->addRow("Placa (Board):", boardLayout);

    auto* searchFeedbackLabel = new QLabel("", &dialog);
    searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #64748B;");
    formLayout->addRow("", searchFeedbackLabel);

    QObject::connect(searchBoardBtn, &QPushButton::clicked, m_mainWindow, [=]() {
        QString query = boardEdit->text().trimmed();
        if (query.isEmpty()) {
            searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #EF4444;");
            searchFeedbackLabel->setText("Digite o nome de uma placa para pesquisar.");
            return;
        }
        
        searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #2563EB;");
        searchFeedbackLabel->setText("Buscando placa no PlatformIO...");
        QCoreApplication::processEvents();
        
        if (!platformIOIsInstalled()) {
            searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #EF4444;");
            searchFeedbackLabel->setText("PlatformIO não instalado!");
            return;
        }
        
        QProcess p;
        QString pioCmd = getPlatformIOCommand();
        if (pioCmd.isEmpty()) pioCmd = "platformio";
        p.setProgram(pioCmd);
        p.setArguments({"boards", query, "--json-output"});
        p.start();
        if (!p.waitForFinished(5000)) {
            searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #EF4444;");
            searchFeedbackLabel->setText("Tempo limite de busca excedido.");
            return;
        }
        
        QByteArray out = p.readAllStandardOutput();
        QJsonParseError err;
        QJsonDocument d = QJsonDocument::fromJson(out, &err);
        if (err.error == QJsonParseError::NoError && d.isArray() && !d.array().isEmpty()) {
            QJsonObject firstMatch = d.array().first().toObject();
            QString id = firstMatch.value("id").toString();
            QString name = firstMatch.value("name").toString();
            
            searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #22C55E;");
            searchFeedbackLabel->setText(QString("Placa encontrada: %1 (%2)").arg(name).arg(id));
            boardEdit->setText(id);
        } else {
            searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #EF4444;");
            searchFeedbackLabel->setText("Placa não encontrada. Verifique a grafia.");
        }
    });

    // Framework Selection
    auto* frameworkCombo = new QComboBox(&dialog);
    frameworkCombo->addItems({"arduino", "espidf"});
    frameworkCombo->setCurrentText(currentFramework);
    formLayout->addRow("Framework:", frameworkCombo);

    // Serial/USB Port
    auto* portCombo = new QComboBox(&dialog);
    portCombo->setEditable(true);
    portCombo->addItem("Auto-Detect");
    
    QStringList activePorts;
#ifdef Q_OS_WIN
    QSettings registry("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM", QSettings::NativeFormat);
    for (const QString& key : registry.allKeys()) {
        QString port = registry.value(key).toString();
        if (!port.isEmpty() && !activePorts.contains(port)) {
            activePorts.append(port);
        }
    }
#endif
    
    if (activePorts.isEmpty()) {
        for (int i = 1; i <= 8; ++i) {
            activePorts.append(QString("COM%1").arg(i));
        }
    }
    portCombo->addItems(activePorts);
    portCombo->setCurrentText(currentPort);
    formLayout->addRow("Porta USB/Serial:", portCombo);

    // Speed Selection
    auto* speedCombo = new QComboBox(&dialog);
    speedCombo->addItems({"Auto", "115200", "921600", "460800", "230400", "512000"});
    speedCombo->setCurrentText(currentSpeed);
    formLayout->addRow("Velocidade (Speed):", speedCombo);

    layout->addLayout(formLayout);

    // Action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancel");
    
    auto* confirmBtn = new QPushButton("Confirmar", &dialog);
    
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(confirmBtn);
    layout->addLayout(buttonLayout);

    QObject::connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(confirmBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) return false;

    outBoard = boardEdit->text();
    outFramework = frameworkCombo->currentText();
    outPort = portCombo->currentText();
    outSpeed = speedCombo->currentText();

    if (mcu) {
        QJsonObject cfg;
        QVariant existing = mcu->property("microcontrollerConfig");
        if (existing.isValid() && existing.canConvert<QString>()) {
            QJsonDocument d = QJsonDocument::fromJson(existing.toString().toUtf8());
            if (d.isObject()) cfg = d.object();
        }
        cfg["board"] = outBoard;
        cfg["core"] = outFramework;
        cfg["upload_port"] = outPort;
        cfg["upload_speed"] = outSpeed;
        mcu->setProperty("microcontrollerConfig", QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)));
    }

    return true;
}

void ToolchainManager::platformIOConfigTriggered() {
    QString b, f, p, s;
    if (showPlatformIOConfigDialog(b, f, p, s)) {
        logMessage(QString("PlatformIO configurado: Placa=%1, Framework=%2, Porta=%3, Velocidade=%4")
                   .arg(b, f, p, s), "SUCCESS");
    }
}

void ToolchainManager::parseResourceUsage(const QString& line) {
    static QRegularExpression ramRegex("RAM:\\s+\\[.*\\]\\s+([0-9.]+)\\%");
    QRegularExpressionMatch ramMatch = ramRegex.match(line);
    if (ramMatch.hasMatch() && m_ramProgressBar) {
        bool ok = false;
        double pct = ramMatch.captured(1).toDouble(&ok);
        if (ok) {
            m_ramProgressBar->setValue(qRound(pct));
            m_ramProgressBar->setFormat(QString("%1%").arg(pct, 0, 'f', 1));
            m_ramProgressBar->setToolTip(line.trimmed());
        }
    }

    static QRegularExpression flashRegex("Flash:\\s+\\[.*\\]\\s+([0-9.]+)\\%");
    QRegularExpressionMatch flashMatch = flashRegex.match(line);
    if (flashMatch.hasMatch() && m_flashProgressBar) {
        bool ok = false;
        double pct = flashMatch.captured(1).toDouble(&ok);
        if (ok) {
            m_flashProgressBar->setValue(qRound(pct));
            m_flashProgressBar->setFormat(QString("%1%").arg(pct, 0, 'f', 1));
            m_flashProgressBar->setToolTip(line.trimmed());
        }
    }
}

