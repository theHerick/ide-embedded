#include "MainWindow.h"
#include "CodeGenerator.h"
#include "PcbExporter.h"
#include "CustomComponent.h"
#include "ComponentCreatorDialog.h"
#include "WebPageEditorDialog.h"
#include "AiOptimizer.h"
#include <QSplitter>
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
class PcbPreviewLabel : public QLabel {
public:
    PcbPreviewLabel(QWidget* parent = nullptr) : QLabel(parent) {
        setMouseTracking(true);
    }

    void setScaleFactors(double displayScale, double mmPerPixel) {
        m_displayScale = displayScale;
        m_mmPerPixel = mmPerPixel;
    }

    void setMeasuring(bool measuring) {
        m_measuring = measuring;
        m_startPoint = QPoint();
        m_endPoint = QPoint();
        setCursor(m_measuring ? Qt::CrossCursor : Qt::ArrowCursor);
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (m_measuring && event->button() == Qt::LeftButton) {
            m_startPoint = event->pos();
            m_endPoint = event->pos();
            update();
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_measuring && !m_startPoint.isNull()) {
            m_endPoint = event->pos();
            update();
        }
        QLabel::mouseMoveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        QLabel::paintEvent(event);
        if (m_measuring && !m_startPoint.isNull()) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            
            QPen pen(QColor(239, 68, 68), 2, Qt::SolidLine); // Red line
            painter.setPen(pen);
            painter.drawLine(m_startPoint, m_endPoint);
            
            // Markers
            painter.setBrush(QColor(239, 68, 68));
            painter.drawEllipse(m_startPoint, 3, 3);
            painter.drawEllipse(m_endPoint, 3, 3);
            
            double dx = (m_endPoint.x() - m_startPoint.x()) / m_displayScale;
            double dy = (m_endPoint.y() - m_startPoint.y()) / m_displayScale;
            double distMm = sqrt(dx*dx + dy*dy) * m_mmPerPixel;
            
            QString text = QString("%1 mm").arg(distMm, 0, 'f', 2);
            
            painter.setPen(Qt::white);
            painter.setBrush(QColor(15, 23, 42)); // Dark background for label
            QRect textRect = painter.fontMetrics().boundingRect(text).adjusted(-6, -3, 6, 3);
            textRect.moveCenter(m_endPoint + QPoint(0, -25));
            painter.drawRoundedRect(textRect, 5, 5);
            painter.drawText(textRect, Qt::AlignCenter, text);
        }
    }

private:
    bool m_measuring = false;
    QPoint m_startPoint;
    QPoint m_endPoint;
    double m_displayScale = 1.0;
    double m_mmPerPixel = 1.0;
};

static QString sanitizeIdentifier(const QString& name) {
    QString res = name.normalized(QString::NormalizationForm_D).toUpper();
    QString clean;
    for (int i = 0; i < res.length(); ++i) {
        QChar c = res.at(i);
        if (c.category() == QChar::Mark_NonSpacing || c.category() == QChar::Mark_SpacingCombining) {
            continue;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            clean.append(c);
        } else if (c.isSpace()) {
            clean.append('_');
        } else if (c == '.' || c == '-') {
            clean.append('_'); // e.g. 4.7K -> 4_7K
        }
    }
    if (clean.isEmpty()) {
        clean = "COMP";
    }
    if (!clean.isEmpty() && clean.at(0).isDigit()) {
        clean.prepend('_');
    }
    return clean;
}

// ─── Global Qt message handler → IDE log panel ────────────────────────────
static MainWindow* g_mainWindowLogTarget = nullptr;

static void ideMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (!g_mainWindowLogTarget) return;
    QString msgType;
    switch (type) {
        case QtDebugMsg:    msgType = "DEBUG"; break;
        case QtInfoMsg:     msgType = "INFO";  break;
        case QtWarningMsg:  msgType = "WARNING"; break;
        case QtCriticalMsg: msgType = "ERROR"; break;
        case QtFatalMsg:    msgType = "ERROR"; break;
    }
    // Use QMetaObject to post to UI thread safely
    QMetaObject::invokeMethod(g_mainWindowLogTarget, "logMessage",
                              Qt::QueuedConnection,
                              Q_ARG(QString, msg),
                              Q_ARG(QString, msgType));
}
// ───────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_scene = new WorkspaceScene(this);
    m_view = new WorkspaceView(m_scene, this);
    m_view->setObjectName("workspaceScene");
    m_blockEditor = new BlockEditor(this);
    m_simulator = new HardwareSimulator(this);
    m_aiOptimizer = new AiOptimizer(this);

    // Dynamic compilation on block updates
    connect(m_blockEditor, &BlockEditor::blocksChanged, this, &MainWindow::compileCode);

    buildLayout();
    buildToolbar();
    applyTheme();
    loadToolboxItems();

    // Install Qt message handler to route qDebug/qWarning/qCritical → IDE log panel
    g_mainWindowLogTarget = this;
    qInstallMessageHandler(ideMessageHandler);

    // Default starting node
    m_scene->addComponent("esp32", "Controlador ESP32", QPointF(0, 0));

    // Connect default signals
    connect(m_scene, &WorkspaceScene::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_scene, &WorkspaceScene::rightClickedComponent, this, &MainWindow::showComponentContextMenu);
    connect(m_scene, &WorkspaceScene::componentAdded, this, &MainWindow::onComponentAdded);
    connect(m_view, &WorkspaceView::requestComponentCreation, this, &MainWindow::openComponentCreator);
    connect(m_scene, &WorkspaceScene::doubleClickedComponent, this, [this](ComponentItem* comp, const QPointF&) {
        if (!comp) return;
        if (comp->componentType() == "resistor") {
            auto* res = static_cast<ResistorItem*>(comp);
            this->editResistorValue(res);
        } else if (comp->componentType() == "capacitor") {
            auto* cap = static_cast<CapacitorItem*>(comp);
            this->editCapacitorProperties(cap);
        } else if (comp->componentType() == "potentiometer") {
            auto* pot = static_cast<PotentiometerItem*>(comp);
            this->editPotentiometerValue(pot);
        } else if (comp->componentType() == "ldr") {
            auto* ldr = static_cast<LdrItem*>(comp);
            this->editLdrValue(ldr);
            if (m_tutorialOverlay && m_tutorialOverlay->isVisible() && m_activeTutorial == 4) {
                if (m_tutorialOverlay->currentStep() == 27) {
                    m_tutorialOverlay->advance();
                }
            }
        } else if (comp->componentType() == "motor") {
            auto* motor = static_cast<MotorItem*>(comp);
            this->editMotorProperties(motor);
        } else if (comp->componentType() == "esp32") {
            this->openEventEditor(comp, "aoIniciar");
        } else if (comp->componentType() == "led" || comp->componentType() == "rgb_led") {
            this->openEventEditor(comp, "aoLigar");
        } else if (comp->componentType() == "button") {
            this->openEventEditor(comp, "aoClicar");
        } else if (comp->componentType() == "buzzer") {
            this->openEventEditor(comp, "aoTocar");
        } else if (comp->componentType() == "dht22") {
            auto* dht = static_cast<DHT22Item*>(comp);
            this->editDHT22Properties(dht);
        } else if (comp->componentType() == "hcsr04") {
            auto* hcsr = static_cast<HCSR04Item*>(comp);
            this->editHCSR04Properties(hcsr);
            if (m_tutorialOverlay && m_tutorialOverlay->isVisible() && m_activeTutorial == 2) {
                if (m_tutorialOverlay->currentStep() == 24) {
                    m_tutorialOverlay->advance();
                }
            }
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            if (custom->category() == "analog_input") {
                this->editCustomPotentiometerValue(custom);
            } else {
                QString primaryEvent = "aoIniciar";
                QString category = custom->category();
                if (category == "digital_trigger") primaryEvent = "aoClicar";
                else if (category == "digital_actuator") primaryEvent = "aoLigar";
                else if (category == "analog_input") primaryEvent = "aoGirar";
                else if (category == "active_actuator") primaryEvent = "aoTocar";
                else if (!custom->definition().customEvents.isEmpty()) {
                    primaryEvent = custom->definition().customEvents.first().callback;
                }
                this->openEventEditor(custom, primaryEvent);
            }
        }
    });

    compileCode();
    
    // Select ESP32 by default at startup to show the block editor immediately!
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "esp32") {
            comp->setSelected(true);
            onSelectionChanged(comp);
            break;
        }
    }

    // Start with the block editor hidden; it will open only when the user
    // explicitly requests an event (via context menu or double-click).
    if (m_blockEditor) {
        m_blockEditor->hide();
        m_blockEditor->setEnabled(false);
    }

    // Check for toolchain at startup
    QTimer::singleShot(2000, this, [this]() { ToolchainManager(this).checkAndInstallToolchain(); });

    // Onboarding / Tutorial auto-advance connections
    connect(m_scene, &WorkspaceScene::componentAdded, this, [this](ComponentItem* comp) {
        if (!m_tutorialOverlay || !m_tutorialOverlay->isVisible() || !comp) return;
        int step = m_tutorialOverlay->currentStep();
        QString type = comp->componentType();

        if (m_activeTutorial == 1) {
            // Tutorial 1: LED (step 1), Resistor (step 2), Button (step 7)
            if      (step == 1 && type == "led")      m_tutorialOverlay->advance();
            else if (step == 2 && type == "resistor")  m_tutorialOverlay->advance();
            else if (step == 7 && type == "button")    m_tutorialOverlay->advance();
        } else if (m_activeTutorial == 2) {
            // Tutorial 2: HC-SR04 (step 3), Buzzer (step 8)
            if      (step == 3 && (type == "hcsr04" || type == "hc-sr04" || type == "ultrasonic"))
                m_tutorialOverlay->advance();
            else if (step == 8 && type == "buzzer")
                m_tutorialOverlay->advance();
        } else if (m_activeTutorial == 3) {
            // Tutorial 3: Servomotor (step 1)
            if      (step == 1 && (type == "sg90" || type == "servo" || type == "motor"))
                m_tutorialOverlay->advance();
        } else if (m_activeTutorial == 4) {
            // Tutorial 4: LDR (step 1), Resistor (step 2), Relay (step 8), Lamp (step 12)
            if      (step == 1 && (type == "ldr" || type == "light_sensor"))
                m_tutorialOverlay->advance();
            else if (step == 2 && type == "resistor")
                m_tutorialOverlay->advance();
            else if (step == 8 && (type == "relay" || type == "rele"))
                m_tutorialOverlay->advance();
            else if (step == 12 && (type == "lamp" || type == "lampada" || type == "lÃ¢mpada"))
                m_tutorialOverlay->advance();
        }
    });

    connect(m_scene, &WorkspaceScene::cableAdded, this, [this]() {
        if (!m_tutorialOverlay || !m_tutorialOverlay->isVisible()) return;
        int step = m_tutorialOverlay->currentStep();

        if (m_activeTutorial == 1) {
            // Tutorial 1: connection steps 4, 5, 6, 8, 9
            if (step == 4 || step == 5 || step == 6 || step == 8 || step == 9)
                m_tutorialOverlay->advance();
        } else if (m_activeTutorial == 2) {
            // Tutorial 2: connection steps 4,5,6,7,9,10
            if (step == 4 || step == 5 || step == 6 || step == 7 || step == 9 || step == 10)
                m_tutorialOverlay->advance();
        } else if (m_activeTutorial == 3) {
            // Tutorial 3: connection steps 2, 3, 4
            if (step == 2 || step == 3 || step == 4)
                m_tutorialOverlay->advance();
        } else if (m_activeTutorial == 4) {
            // Tutorial 4: connection steps 4, 5, 6, 7, 9, 10, 11, 13
            if (step == 4 || step == 5 || step == 6 || step == 7 || step == 9 || step == 10 || step == 11 || step == 13)
                m_tutorialOverlay->advance();
        }
    });

    connect(m_buildAction, &QAction::triggered, this, [this]() {
        if (!m_tutorialOverlay || !m_tutorialOverlay->isVisible()) return;
        int step = m_tutorialOverlay->currentStep();
        if ((m_activeTutorial == 1 && step == 13) ||
            (m_activeTutorial == 2 && step == 22) ||
            (m_activeTutorial == 3 && step == 15) ||
            (m_activeTutorial == 4 && step == 25))
            m_tutorialOverlay->advance();
    });

    connect(m_playAction, &QAction::triggered, this, [this]() {
        if (!m_tutorialOverlay || !m_tutorialOverlay->isVisible()) return;
        int step = m_tutorialOverlay->currentStep();
        if ((m_activeTutorial == 1 && step == 14) ||
            (m_activeTutorial == 2 && step == 23) ||
            (m_activeTutorial == 3 && step == 16) ||
            (m_activeTutorial == 4 && step == 26))
            m_tutorialOverlay->advance();
    });

    connect(m_blockEditor, &BlockEditor::blocksChanged, this, [this]() {
        checkBlockEditorTutorialSteps();
    });

    logMessage("IDE Embedded inicializada com sucesso.", "SYSTEM");
    statusBar()->showMessage("IDE Embedded pronta");
}

MainWindow::~MainWindow() {
    g_mainWindowLogTarget = nullptr;
    qInstallMessageHandler(nullptr);
}

void MainWindow::buildLayout() {
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Splitter separating: Left (Toolbox + Viewport) and Right (Block Editor)
    m_mainSplitter = new QSplitter(Qt::Horizontal, central);
    mainLayout->addWidget(m_mainSplitter);

    // Left container splits into: Toolbox, Viewport (Center) and Console (Bottom)
    auto* leftContainer = new QWidget(m_mainSplitter);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* leftInnerSplitter = new QSplitter(Qt::Vertical, leftContainer);
    leftLayout->addWidget(leftInnerSplitter);

    // Visual Canvas & Toolbox horizontal layout
    auto* canvasToolboxWidget = new QWidget(leftInnerSplitter);
    auto* canvasToolboxLayout = new QHBoxLayout(canvasToolboxWidget);
    canvasToolboxLayout->setContentsMargins(0, 0, 0, 0);
    canvasToolboxLayout->setSpacing(0);

    // Sidebar Container
    auto* sidebarContainer = new QWidget(canvasToolboxWidget);
    sidebarContainer->setFixedWidth(180);
    auto* sidebarLayout = new QVBoxLayout(sidebarContainer);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    // Headers row widget
    auto* headerWidget = new QWidget(sidebarContainer);
    headerWidget->setStyleSheet("background-color: #FFFFFF; border-bottom: 1px solid #E6EEF3; border-right: 1px solid #E6EEF3;");
    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(12, 10, 12, 10);
    headerLayout->setSpacing(6);

    auto* headerLabel = new QLabel("TOOLBOX", headerWidget);
    headerLabel->setStyleSheet("color: #94A3B8; font-size: 10px; font-weight: 800; letter-spacing: 1.5px;");
    headerLayout->addWidget(headerLabel);
    headerLayout->addStretch();

    auto* modelarBtn = new QPushButton("+ Modelar", headerWidget);
    modelarBtn->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F0F9FF, stop:0.45 #E0F2FE, stop:0.46 #BAE6FD, stop:1 #7DD3FC); "
        "  border: 1.5px solid #0284C7; "
        "  border-radius: 6px; "
        "  padding: 4px 8px; "
        "  font-weight: bold; "
        "  color: #0369A1; "
        "  font-size: 10px; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E0F2FE, stop:0.45 #BAE6FD, stop:0.46 #7DD3FC, stop:1 #38BDF8); "
        "  border-color: #0369A1; "
        "  color: #075985; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7DD3FC, stop:1 #0284C7); "
        "  color: white; "
        "}"
    );
    connect(modelarBtn, &QPushButton::clicked, this, &MainWindow::openComponentCreator);
    headerLayout->addWidget(modelarBtn);

    sidebarLayout->addWidget(headerWidget);

    // Sidebar Component Drawer List
    m_toolboxList = new QListWidget(sidebarContainer);
    m_toolboxList->setObjectName("toolboxList");
    m_toolboxList->setContextMenuPolicy(Qt::CustomContextMenu);
    sidebarLayout->addWidget(m_toolboxList);

    sidebarContainer->hide(); // Hide the components toolbox sidebar completely!

    // Do not add the sidebarContainer to the layout, only add the visual workspace
    canvasToolboxLayout->addWidget(m_view);
    
    connect(m_toolboxList, &QListWidget::itemDoubleClicked, this, &MainWindow::onAddComponentClicked);
    connect(m_toolboxList, &QListWidget::customContextMenuRequested, this, &MainWindow::onToolboxContextMenu);

    // ── Bottom panel: tabbed Console + Oscilloscope ──────────────────────────
    m_oscilloscope = new OscilloscopePanel(leftInnerSplitter);

    m_bottomTabs = new QTabWidget(leftInnerSplitter);
    m_bottomTabs->setObjectName("bottomTabs");
    m_bottomTabs->setMaximumHeight(220);
    m_bottomTabs->setStyleSheet(
        "QTabWidget::pane { border-top: 1px solid #CBD5E1; background: #FFFFFF; }"
        "QTabBar { background: #EAF0F6; }"
        "QTabBar::tab {"
        "  background: #E2E8F0; color: #475569;"
        "  border: 1px solid #CBD5E1; border-bottom: none;"
        "  border-top-left-radius: 4px; border-top-right-radius: 4px;"
        "  padding: 6px 16px; font-size: 10px; font-weight: 600;"
        "  font-family: 'Segoe UI', Arial; margin-right: 2px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #FFFFFF; color: #4F46E5;"
        "  border-color: #CBD5E1; border-bottom: 1px solid #FFFFFF;"
        "}"
        "QTabBar::tab:hover { color: #4F46E5; background: #F1F5F9; }"
    );

    m_compilerConsole = new QPlainTextEdit(m_bottomTabs);
    m_compilerConsole->setReadOnly(true);
    m_compilerConsole->setPlaceholderText("Logs da IDE Embedded - Monitorando eventos e acoes...");

    m_serialMonitor = new QPlainTextEdit(m_bottomTabs);
    m_serialMonitor->setReadOnly(true);
    m_serialMonitor->setPlaceholderText("Aguardando saída serial...");
    m_serialMonitor->setStyleSheet("background-color: #FFFFFF; color: #1E293B; border: 1px solid #CBD5E1; font-family: 'Fira Code', 'Consolas', monospace; font-size: 13px;");

    m_bottomTabs->addTab(m_compilerConsole, "Console");
    m_bottomTabs->addTab(m_serialMonitor, "Monitor Serial");
    m_bottomTabs->addTab(m_oscilloscope,    "Osciloscopio");

    connect(m_bottomTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (m_tutorialOverlay && m_tutorialOverlay->isVisible() && m_activeTutorial == 2) {
            if (m_tutorialOverlay->currentStep() == 25 && index == 2) { // Index 2 is Osciloscopio
                m_tutorialOverlay->advance();
            }
        }
    });

    leftInnerSplitter->addWidget(canvasToolboxWidget);
    leftInnerSplitter->addWidget(m_bottomTabs);

    // Set layout sizes: favor workspace and keep bottom panel compact
    QList<int> innerSizes;
    innerSizes << 600 << 160;
    leftInnerSplitter->setSizes(innerSizes);

    // Right side: visual event blocks (shown by default)
    m_mainSplitter->addWidget(leftContainer);
    m_mainSplitter->addWidget(m_blockEditor);

    // Set splitter sizes (50% left container, 50% block editor)
    QList<int> outerSizes;
    outerSizes << 500 << 500;
    m_mainSplitter->setSizes(outerSizes);

    // Wire simulator -> oscilloscope & serial monitor
    connect(m_simulator, &HardwareSimulator::pinStateChanged,
            m_oscilloscope, &OscilloscopePanel::onPinStateChanged);
    connect(m_simulator, &HardwareSimulator::simulationStopped,
            m_oscilloscope, &OscilloscopePanel::onSimulationStopped);
            
    connect(m_simulator, &HardwareSimulator::serialPrint, this, [this](const QString& text) {
        if (m_serialMonitor) {
            m_serialMonitor->moveCursor(QTextCursor::End);
            m_serialMonitor->insertPlainText(text);
        }
    });

    connect(m_simulator, &HardwareSimulator::serialMessage, this, [this](const QString& msg, const QString& type) {
        logMessage(msg, type);
    });

    // Resource Monitor Widget (RAM & Flash)
    m_resourceBarWidget = new QWidget(this);
    auto* resourceLayout = new QHBoxLayout(m_resourceBarWidget);
    resourceLayout->setContentsMargins(6, 0, 16, 0); // sleek, integrated margins
    resourceLayout->setSpacing(8);

    auto* ramLbl = new QLabel("RAM:", m_resourceBarWidget);
    ramLbl->setStyleSheet("color: #475569; font-weight: 700; font-size: 10px; font-family: 'Segoe UI', Arial;");
    resourceLayout->addWidget(ramLbl);

    m_ramProgressBar = new QProgressBar(m_resourceBarWidget);
    m_ramProgressBar->setObjectName("ramBar");
    m_ramProgressBar->setRange(0, 100);
    m_ramProgressBar->setValue(0);
    m_ramProgressBar->setTextVisible(true);
    m_ramProgressBar->setFormat("0.0%");
    m_ramProgressBar->setToolTip("Consumo de Memória RAM da ESP32");
    resourceLayout->addWidget(m_ramProgressBar);

    auto* flashLbl = new QLabel("Flash:", m_resourceBarWidget);
    flashLbl->setStyleSheet("color: #475569; font-weight: 700; font-size: 10px; font-family: 'Segoe UI', Arial;");
    resourceLayout->addWidget(flashLbl);

    m_flashProgressBar = new QProgressBar(m_resourceBarWidget);
    m_flashProgressBar->setObjectName("flashBar");
    m_flashProgressBar->setRange(0, 100);
    m_flashProgressBar->setValue(0);
    m_flashProgressBar->setTextVisible(true);
    m_flashProgressBar->setFormat("0.0%");
    m_flashProgressBar->setToolTip("Consumo de Memória Flash da ESP32");
    resourceLayout->addWidget(m_flashProgressBar);

    statusBar()->addPermanentWidget(m_resourceBarWidget);
}

void MainWindow::buildToolbar() {
    auto* toolbar = addToolBar("Ações Principais");
    toolbar->setMovable(false);
    // Use icons only as requested
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(26, 26));

    m_newAction = toolbar->addAction("");
    m_newAction->setToolTip("Novo Projeto");
    m_newAction->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newProject);

    m_openAction = toolbar->addAction("");
    m_openAction->setToolTip("Abrir Projeto");
    m_openAction->setIcon(QIcon(":/icons/pasta.png"));
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openProject);
    if (auto* tb = qobject_cast<QToolButton*>(toolbar->widgetForAction(m_openAction))) {
        tb->setIconSize(QSize(32, 32));
    }

    m_saveAction = toolbar->addAction("");
    m_saveAction->setToolTip("Salvar Projeto");
    m_saveAction->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveProject);

    toolbar->addSeparator();

    // Undo/Redo actions in toolbar
    m_undoAction = m_scene->undoStack()->createUndoAction(this, "");
    m_undoAction->setToolTip("Desfazer (Ctrl+Z)");

    // Initial icon state
    m_undoAction->setIcon(QIcon(":/icons/undo_off.png"));
    toolbar->addAction(m_undoAction);

    m_redoAction = m_scene->undoStack()->createRedoAction(this, "");
    m_redoAction->setToolTip("Refazer (Ctrl+Y)");
    m_redoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    toolbar->addAction(m_redoAction);

    // Update Undo icon dynamically based on stack state
    connect(m_scene->undoStack(), &QUndoStack::canUndoChanged, this, [this](bool canUndo) {
        m_undoAction->setIcon(canUndo ? QIcon(":/icons/undo_on.png") : QIcon(":/icons/undo_off.png"));
    });

    // Redo icons: Flip the Undo icons horizontally for consistency
    QImage redoOnImg(":/icons/undo_on.png");
    QImage redoOffImg(":/icons/undo_off.png");
    QIcon redoOnIcon(QPixmap::fromImage(redoOnImg.transformed(QTransform().scale(-1, 1))));
    QIcon redoOffIcon(QPixmap::fromImage(redoOffImg.transformed(QTransform().scale(-1, 1))));

    m_redoAction->setIcon(m_scene->undoStack()->canRedo() ? redoOnIcon : redoOffIcon);

    connect(m_scene->undoStack(), &QUndoStack::canRedoChanged, this, [this, redoOnIcon, redoOffIcon](bool canRedo) {
        m_redoAction->setIcon(canRedo ? redoOnIcon : redoOffIcon);
    });
    toolbar->addSeparator();

    // Build action: must be used before simulation is allowed
    m_buildAction = toolbar->addAction("");
    m_buildAction->setToolTip("Build (Compilar projeto)");
    m_buildAction->setIcon(QIcon(":/icons/build.png"));
    connect(m_buildAction, &QAction::triggered, this, &MainWindow::buildProject);

    m_playAction = toolbar->addAction("");
    m_playAction->setToolTip("Iniciar/Parar Simulação de Hardware");
    m_playAction->setCheckable(true);
    m_playAction->setIcon(QIcon(":/icons/play.svg"));
    m_playAction->setEnabled(true);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::toggleSimulation);
    updatePlayActionState();

    // PlatformIO Upload action (disk + DVD icon)
    auto* uploadAction = toolbar->addAction("");
    uploadAction->setToolTip("Gravar na placa (Upload via PlatformIO)");
    uploadAction->setIcon(style()->standardIcon(QStyle::SP_DriveCDIcon));
    connect(uploadAction, &QAction::triggered, this, [this]() { ToolchainManager(this).platformIOUpload(); });
    uploadAction->setEnabled(true);

    // Style the toolbar button to match the requested UI (larger primary action)
    QWidget* playWidget = toolbar->widgetForAction(m_playAction);
    if (auto* tb = qobject_cast<QToolButton*>(playWidget)) {
        // Use icon-only style to match other toolbar icons
        tb->setAutoRaise(true);
        tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
        // keep action checked state reflected on the button
        connect(m_playAction, &QAction::toggled, tb, &QToolButton::setChecked);
    }

    toolbar->addSeparator();

    auto* exportLaserAction = toolbar->addAction("");
    exportLaserAction->setToolTip("Exportar trilhas para Laser");
    exportLaserAction->setIcon(style()->standardIcon(QStyle::SP_FileLinkIcon));
    connect(exportLaserAction, &QAction::triggered, this, &MainWindow::exportLaserPNG);

    auto* pioConfigAction = toolbar->addAction("");
    pioConfigAction->setToolTip("Configurações do PlatformIO (Placa e Porta USB)");
    pioConfigAction->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    connect(pioConfigAction, &QAction::triggered, this, [this]() { ToolchainManager(this).platformIOConfigTriggered(); });

    toolbar->addSeparator();

    m_webPageAction = toolbar->addAction("");
    m_webPageAction->setToolTip("Construtor Web Page / Dashboard");
    m_webPageAction->setIcon(QIcon(":/icons/webPage.ico"));
    connect(m_webPageAction, &QAction::triggered, this, [this]() {
        QStringList availableVars;
        for (auto* item : m_scene->items()) {
            if (auto* c = dynamic_cast<ComponentItem*>(item)) {
                if (c->componentType() == "dht22") {
                    availableVars.append("umidade");
                    availableVars.append("temperatura");
                } else if (c->componentType() == "hcsr04") {
                    availableVars.append("distancia");
                }
            }
        }
        
        // Extract variables created in BlockEditor (scope variables)
        if (m_blockEditor) {
            auto eventStorage = m_blockEditor->getEventBlockStorage();
            for (auto it = eventStorage.begin(); it != eventStorage.end(); ++it) {
                for (const auto& block : it.value()) {
                    if (block.type == LogicBlockType::CREATE_VAR) {
                        QString varName = block.createVarName.trimmed().remove(" ");
                        if (!varName.isEmpty()) {
                            availableVars.append(varName);
                        }
                    }
                }
            }
        }
        
        availableVars.removeDuplicates();
        
        WebPageEditorDialog dlg(m_webPageData, availableVars, this);
        
        bool overlayWasActive = m_tutorialOverlay && m_tutorialOverlay->isVisible() && m_activeTutorial == 3 && (m_tutorialOverlay->currentStep() == 5 || m_tutorialOverlay->currentStep() == 12);
        if (overlayWasActive) {
            m_tutorialOverlay->advance(); 
            m_tutorialOverlay->setParent(&dlg);
            m_tutorialOverlay->resize(dlg.size());
            m_tutorialOverlay->show();
            m_tutorialOverlay->raise();
        }

        dlg.exec();
        
        if (overlayWasActive && m_tutorialOverlay) {
            m_tutorialOverlay->setParent(this);
            m_tutorialOverlay->resize(this->size());
            m_tutorialOverlay->show();
            m_tutorialOverlay->raise();
            
            if (m_activeTutorial == 3 && m_tutorialOverlay->currentStep() == 13) {
                m_tutorialOverlay->advance(); // advance to step 14
            }
        }

        QString editId = dlg.getEditEventCompId();
        QString editEvent = dlg.getEditEventName();
        if (!editId.isEmpty() && !editEvent.isEmpty()) {
            openWebEventEditor(editId, editEvent);
        }
    });

    m_clearAction = toolbar->addAction("");
    m_clearAction->setToolTip("Limpar Workspace");
    m_clearAction->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(m_clearAction, &QAction::triggered, this, &MainWindow::clearScene);

    toolbar->addSeparator();

    // + Modelar action in the toolbar
    auto* modelarAction = toolbar->addAction("");
    modelarAction->setToolTip("Modelar componente");
    modelarAction->setIcon(QIcon(":/icons/modelar.png"));
    connect(modelarAction, &QAction::triggered, this, &MainWindow::openComponentCreator);

    toolbar->addSeparator();

    // Info action: show project/firmware information (placed as last toolbar button)
    auto* infoAction = toolbar->addAction("");
    infoAction->setToolTip("Informações do Software");
    infoAction->setIcon(style()->standardIcon(QStyle::SP_MessageBoxQuestion));
    connect(infoAction, &QAction::triggered, this, &MainWindow::showFirmwareInfo);

    // Project menu at the top
    QMenu* projectMenu = menuBar()->addMenu("Projeto");
    projectMenu->setStyleSheet(
        "QMenu { background: #FBFBFB; border: 1px solid #E6EEF3; color: #0F172A; padding: 4px; }"
        "QMenu::item { padding: 6px 20px; }"
        "QMenu::item:selected { background: #EEF2FF; color: #1D4ED8; }"
    );
    QAction* menuNewProject = projectMenu->addAction("Novo Projeto");
    QAction* menuOpenProject = projectMenu->addAction("Abrir Projeto...");
    QAction* menuSaveProject = projectMenu->addAction("Salvar Projeto");
    QAction* menuSaveProjectAs = projectMenu->addAction("Salvar Projeto Como...");
    connect(menuNewProject, &QAction::triggered, this, &MainWindow::newProject);
    connect(menuOpenProject, &QAction::triggered, this, &MainWindow::openProject);
    connect(menuSaveProject, &QAction::triggered, this, &MainWindow::saveProject);
    connect(menuSaveProjectAs, &QAction::triggered, this, &MainWindow::saveProjectAs);

    // Edit menu
    QMenu* editMenu = menuBar()->addMenu("Editar");
    editMenu->setStyleSheet(projectMenu->styleSheet());
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    QAction* menuDeleteAction = editMenu->addAction("Deletar Seleção");
    menuDeleteAction->setShortcut(QKeySequence::Delete);
    connect(menuDeleteAction, &QAction::triggered, m_scene, &WorkspaceScene::deleteSelected);

    // + Modelar action in the menu bar at the top
    QMenu* componentsMenu = menuBar()->addMenu("Modelagem");
    componentsMenu->setStyleSheet(
        "QMenu { background: #FBFBFB; border: 1px solid #E6EEF3; color: #0F172A; padding: 4px; }"
        "QMenu::item { padding: 6px 20px; }"
        "QMenu::item:selected { background: #EEF2FF; color: #1D4ED8; }"
    );
    QAction* menuModelarAction = componentsMenu->addAction("+ Modelar Novo Componente");
    connect(menuModelarAction, &QAction::triggered, this, &MainWindow::openComponentCreator);

    // View C action directly in the menu bar at the top
    QAction* viewCodeAction = menuBar()->addAction("Visualizar C");
    viewCodeAction->setToolTip("Visualizar o código C++ gerado em tempo real");
    connect(viewCodeAction, &QAction::triggered, this, &MainWindow::viewCompiledCodeModal);

    // Help menu
    m_adjustsMenu = menuBar()->addMenu("Ajustes");
    m_adjustsMenu->setStyleSheet(componentsMenu->styleSheet());
    connect(m_adjustsMenu, &QMenu::aboutToShow, this, [this]() {
        if (m_tutorialOverlay && m_tutorialOverlay->isVisible() && m_activeTutorial == 2) {
            if (m_tutorialOverlay->currentStep() == 1) {
                m_tutorialOverlay->advance();
            }
        }
    });

    QAction* menuSmartConnection = m_adjustsMenu->addAction("Conexão inteligente");
    menuSmartConnection->setCheckable(true);
    connect(menuSmartConnection, &QAction::toggled, m_scene, &WorkspaceScene::setSmartConnectionEnabled);

    m_multitaskAction = m_adjustsMenu->addAction("Multitarefa (FreeRTOS)");
    m_multitaskAction->setCheckable(true);
    m_multitaskAction->setChecked(m_multitaskingEnabled);
    connect(m_multitaskAction, &QAction::toggled, this, [this](bool enabled) {
        m_multitaskingEnabled = enabled;
        CodeGenerator::setMultitaskingEnabled(enabled);
        compileCode(); // Regenerate C++ preview
        if (m_tutorialOverlay && m_tutorialOverlay->isVisible() && m_activeTutorial == 2) {
            if (m_tutorialOverlay->currentStep() == 2 && !enabled) {
                m_tutorialOverlay->advance();
            }
        }
    });

    // Menu dedicado para a IA (Copilot)
    m_aiMenu = menuBar()->addMenu("IA");
    m_aiMenu->setStyleSheet(componentsMenu->styleSheet());

    QAction* verifyCircuitAction = m_aiMenu->addAction("Verificar Circuito (Linting Físico)");
    connect(verifyCircuitAction, &QAction::triggered, this, &MainWindow::verifyCircuitWithAi);

    m_aiMenu->addSeparator();

    m_aiOptimizationAction = m_aiMenu->addAction("Modo Otimização (Copilot)");
    m_aiOptimizationAction->setCheckable(true);
    m_aiOptimizationAction->setChecked(false);

    QAction* configureAiAction = m_aiMenu->addAction("Configurar Token da IA");
    connect(configureAiAction, &QAction::triggered, this, &MainWindow::openAiSettingsDialog);
}

void MainWindow::applyTheme() {
    // Clean, light professional IDE theme
    setStyleSheet(
        "QMainWindow { background: #EAF0F6; }"
        "QMenuBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #E7EEF6); color: #111827; font-family: 'Segoe UI', Arial, sans-serif; font-size: 12px; font-weight: 600; border-bottom: 1px solid #CAD6E2; }"
        "QMenuBar::item { background: transparent; padding: 6px 14px; border-radius: 5px; color: #111827; }"
        "QMenuBar::item:selected { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F7FBFF, stop:1 #DDEAF7); color: #1E40AF; border: 1px solid #B7CBE1; }"
        "QToolBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #DDE7F1); border-bottom: 1px solid #BFCEDC; padding: 8px; spacing: 12px; }"
        "QToolBar::separator { background: #BFCEDC; width: 1px; margin: 5px; }"
        "QToolBar QToolButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F1F5F9, stop:0.46 #E2E8F0, stop:1 #CBD5E1); "
        "  border: 1px solid #AEBFCC; "
        "  border-radius: 9px; "
        "  color: #1E293B; "
        "  padding: 7px 11px; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 11px; "
        "  font-weight: 600; "
        "  min-width: 30px; "
        "  min-height: 30px; "
        "} "
        "QToolBar QToolButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F0F9FF, stop:0.45 #E0F2FE, stop:0.46 #BAE6FD, stop:1 #7DD3FC); "
        "  border-color: #8FB2D8; "
        "  color: #0369A1; "
        "} "
        "QToolBar QToolButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7DD3FC, stop:1 #0284C7); "
        "  border-color: #7FA2C9; "
        "  color: white; "
        "} "
        "QToolBar QToolButton:checked { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #BAE6FD, stop:1 #7DD3FC); "
        "  border-color: #7FA2C9; "
        "} "
        "QListWidget#toolboxList { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #F0F5FA); border-right: 1px solid #C7D4E0; color: #0F172A; font-family: 'Segoe UI', Arial; font-size: 12px; padding: 12px; }"
        "QListWidget#toolboxList::item { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #EDF3F8); border: 1px solid #CAD6E2; border-radius: 9px; padding: 10px; margin-bottom: 8px; color: #0F172A; font-weight: 600; }"
        "QListWidget#toolboxList::item:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #E2ECF6); border-color: #9FBAD4; color: #1D4ED8; }"
        "QListWidget#toolboxList::item:selected { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FBFF, stop:1 #DCEAF8); border-color: #7FA2C9; color: #1D4ED8; }"
        "QSplitter::handle { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #DCE5EE, stop:1 #BFCEDC); }"
        "QPlainTextEdit { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #F1F5F9); border-top: 1px solid #CAD6E2; color: #0B1220; font-family: 'Fira Code', 'Consolas', 'Courier New', monospace; font-size: 13px; padding: 10px; line-height: 1.4; selection-background-color: #DBEAFE; }"
        "QStatusBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FBFE, stop:1 #E4ECF5); color: #475569; border-top: 1px solid #C7D4E0; font-size: 11px; padding-left: 10px; }"
        "QProgressBar#ramBar { border: 1px solid #CBD5E1; border-radius: 4px; background-color: #FFFFFF; text-align: center; font-size: 9px; font-weight: bold; color: #1E293B; height: 13px; width: 75px; }"
        "QProgressBar#ramBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #34D399, stop:1 #059669); border-radius: 3px; }"
        "QProgressBar#flashBar { border: 1px solid #CBD5E1; border-radius: 4px; background-color: #FFFFFF; text-align: center; font-size: 9px; font-weight: bold; color: #1E293B; height: 13px; width: 75px; }"
        "QProgressBar#flashBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #60A5FA, stop:1 #2563EB); border-radius: 3px; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #E2E8F0; min-height: 20px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #CBD5E1; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { border: none; background: none; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 0px; }"
        "QScrollBar::handle:horizontal { background: #E2E8F0; min-width: 20px; border-radius: 4px; }"
        "QScrollBar::handle:horizontal:hover { background: #CBD5E1; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { border: none; background: none; }"
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #93C5FD, stop:0.45 #3B82F6, stop:0.46 #2563EB, stop:1 #1D4ED8); "
        "  border: 1.5px solid #2563EB; "
        "  border-radius: 8px; "
        "  color: white; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-weight: 600; "
        "  font-size: 11px; "
        "  padding: 6px 14px; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3B82F6, stop:0.45 #2563EB, stop:0.46 #1D4ED8, stop:1 #1E40AF); "
        "  border-color: #1D4ED8; "
        "  color: white; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1D4ED8, stop:1 #1E3A8A); "
        "  color: white; "
        "} "
        "QPushButton#cancelBtn, QPushButton#cancel, QPushButton#discard { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F8FAFC, stop:0.46 #F1F5F9, stop:1 #E2E8F0); "
        "  border: 1.5px solid #CBD5E1; "
        "  color: #475569; "
        "} "
        "QPushButton#cancelBtn:hover, QPushButton#cancel:hover, QPushButton#discard:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FAFC, stop:0.45 #F1F5F9, stop:0.46 #E2E8F0, stop:1 #CBD5E1); "
        "  border-color: #94A3B8; "
        "  color: #0F172A; "
        "} "
        "QPushButton#cancelBtn:pressed, QPushButton#cancel:pressed, QPushButton#discard:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E2E8F0, stop:1 #94A3B8); "
        "} "
        "QDialog, QMessageBox { "
        "  background: #F8FAFC; "
        "  color: #0F172A; "
        "}"
        "QMessageBox QLabel { color: #0F172A; }"
    );
}

void MainWindow::onAddComponentClicked(QListWidgetItem* item) {
    if (m_simulator && m_simulator->isRunning()) {
        statusBar()->showMessage("Não é possível adicionar componentes durante a simulação.", 3000);
        return;
    }
    QString type = item->data(Qt::UserRole).toString();
    QString name = item->text(); // Direct text without emojis
    
    // Spawn at the center of the graphics scene
    QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
    m_scene->addComponent(type, name, center);
    statusBar()->showMessage(QString("Componente '%1' adicionado").arg(name), 2000);
}

void MainWindow::onSelectionChanged(ComponentItem* selectedComp) {
    m_selectedComponent = selectedComp;

    if (!selectedComp) {
        statusBar()->showMessage("Selecione um componente para ver suas propriedades");
        // If nothing is selected, hide the block editor
        if (m_blockEditor) {
            m_blockEditor->hide();
            m_blockEditor->setEnabled(false);
            if (m_tutorialOverlay && m_tutorialOverlay->isVisible() && m_activeTutorial == 3 && m_tutorialOverlay->currentStep() == 11) {
                m_tutorialOverlay->advance();
            }
        }
        return;
    }

    statusBar()->showMessage(QString("Selecionado: %1 (%2)").arg(selectedComp->name()).arg(selectedComp->id()));
}

void MainWindow::openEventEditor(ComponentItem* comp, const QString& eventName) {
    if (!comp) return;

    if (m_simulator && m_simulator->isRunning()) {
        statusBar()->showMessage("Não é possível editar eventos durante a simulação.", 3000);
        return;
    }

    auto getConnectedGPIO = [&](ComponentItem* target) -> QString {
        QSet<ComponentItem*> visited;
        QList<ComponentItem*> queue;
        queue.append(target);

        auto isMCU = [](ComponentItem* item) {
            QString t = item->componentType().toLower();
            return (t == "esp32" || t == "esp8266" || t == "board" || t == "generic_mcu" || item->id().startsWith("esp32_"));
        };

        while (!queue.isEmpty()) {
            ComponentItem* curr = queue.takeFirst();
            if (visited.contains(curr)) continue;
            visited.insert(curr);

            for (auto* cable : m_scene->cables()) {
                ComponentItem* neighbor = nullptr;
                QString espPinName;

                if (cable->sourceComponent() == curr) {
                    neighbor = cable->targetComponent();
                    espPinName = cable->targetPinName();
                } else if (cable->targetComponent() == curr) {
                    neighbor = cable->sourceComponent();
                    espPinName = cable->sourcePinName();
                }

                if (neighbor) {
                    if (isMCU(neighbor)) {
                        // Exclude power pins
                        if (!espPinName.contains("5V") && !espPinName.contains("3V3") && !espPinName.contains("GND") && !espPinName.contains("VIN")) {
                            return espPinName;
                        }
                    } else {
                        queue.append(neighbor);
                    }
                }
            }
        }
        return QString();
    };

    // Auto load contextual events
    QStringList avLeds;
    QStringList avPots;
    QStringList avBuzzers;
    QStringList avMotors;
    QStringList avDhts;
    QStringList avHcsrs;
    for (auto* c : m_scene->components()) {
        if (c->componentType() == "dht22") avDhts.append(c->name());
        if (c->componentType() == "hcsr04") avHcsrs.append(c->name());

        QString gpio = getConnectedGPIO(c);
        if (gpio.isEmpty()) continue; // Only show components connected to valid GPIOs!

        QString entry = c->name() + "|" + gpio;

        if (c->componentType() == "led" || c->componentType() == "rgb_led" || c->componentType() == "relay") avLeds.append(entry);
        else if (c->componentType() == "potentiometer" || c->componentType() == "ldr") avPots.append(entry);
        else if (c->componentType() == "buzzer") avBuzzers.append(entry);
        else if (c->componentType() == "motor") avMotors.append(entry);
        else if (auto* custom = dynamic_cast<CustomComponentItem*>(c)) {
            if (custom->category() == "digital_actuator") avLeds.append(entry);
            else if (custom->category() == "analog_input") avPots.append(entry);
            else if (custom->category() == "active_actuator") avBuzzers.append(entry);
        }
    }

    // Extract web sliders to register as global variables
    QStringList avSliders;
    QJsonArray webElems = m_webPageData.value("elements").toArray();
    for (const QJsonValue& val : webElems) {
        QJsonObject obj = val.toObject();
        if (obj.value("type").toString() == "Slider") {
            avSliders.append(obj.value("id").toString());
        }
    }

    // Only open the event editor when explicitly requested (context menu / double-click)
    m_blockEditor->loadEventLogic(comp->id(), eventName, avLeds, avPots, avBuzzers, avMotors, avDhts, avHcsrs, avSliders);
    synchronizeLoopBlocks();
    m_blockEditor->show();
    m_blockEditor->setEnabled(true);

    if (m_tutorialOverlay && m_tutorialOverlay->isVisible()) {
        int step = m_tutorialOverlay->currentStep();
        if ((m_activeTutorial == 1 && step == 10) || (m_activeTutorial == 4 && step == 14) || (m_activeTutorial == 2 && step == 11)) {
            m_tutorialOverlay->advance();
            checkBlockEditorTutorialSteps();
        }
    }
}

void MainWindow::openWebEventEditor(const QString& compId, const QString& eventName) {
    QStringList avLeds, avPots, avBuzzers, avMotors, avDhts, avHcsrs;
    for (QGraphicsItem* item : m_scene->items()) {
        auto* c = dynamic_cast<ComponentItem*>(item);
        if (!c) continue;
        if (c->componentType() == "esp32") continue;
        
        if (c->componentType() == "dht22") avDhts.append(c->name());
        else if (c->componentType() == "hcsr04") avHcsrs.append(c->name());
        else if (c->componentType() == "led" || c->componentType() == "rgb_led" || c->componentType() == "relay") avLeds.append(c->name());
        else if (c->componentType() == "potentiometer" || c->componentType() == "ldr") avPots.append(c->name());
        else if (c->componentType() == "buzzer") avBuzzers.append(c->name());
        else if (c->componentType() == "motor") avMotors.append(c->name());
        else if (auto* custom = dynamic_cast<CustomComponentItem*>(c)) {
            if (custom->category() == "digital_actuator") avLeds.append(custom->name());
            else if (custom->category() == "analog_input") avPots.append(custom->name());
            else if (custom->category() == "active_actuator") avBuzzers.append(custom->name());
        }
    }

    // Extract web sliders to register as global variables
    QStringList avSliders;
    QJsonArray webElems = m_webPageData.value("elements").toArray();
    for (const QJsonValue& val : webElems) {
        QJsonObject obj = val.toObject();
        if (obj.value("type").toString() == "Slider") {
            avSliders.append(obj.value("id").toString());
        }
    }

    m_blockEditor->loadEventLogic(compId, eventName, avLeds, avPots, avBuzzers, avMotors, avDhts, avHcsrs, avSliders);
    synchronizeLoopBlocks();
    m_blockEditor->show();
    m_blockEditor->setEnabled(true);

    if (m_tutorialOverlay && m_tutorialOverlay->isVisible() && m_tutorialOverlay->currentStep() == 9) {
        m_tutorialOverlay->advance();
        checkBlockEditorTutorialSteps();
    }
}

void MainWindow::showComponentContextMenu(ComponentItem* comp, const QPointF& globalPos) {
    if (!comp) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #FBFBFB; border: 1px solid #E2E8F0; color: #0F172A; padding: 4px; }"
        "QMenu::item { padding: 6px 20px; }"
        "QMenu::item:selected { background: #EEF2FF; color: #1D4ED8; }"
    );

    if (comp->componentType() == "button") {
        QAction* actPress = menu.addAction("Evento: Ao Pressionar (aoPressionar)");
        connect(actPress, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoPressionar");
        });
        QAction* actRelease = menu.addAction("Evento: Ao Soltar (aoSoltar)");
        connect(actRelease, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoSoltar");
        });
        QAction* act = menu.addAction("Evento: Ao Clicar (aoClicar)");
        connect(act, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoClicar");
        });
    } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
        QString category = custom->category();
        if (category == "digital_trigger") {
            QAction* act = menu.addAction("Evento: Ao Clicar (aoClicar)");
            connect(act, &QAction::triggered, this, [this, custom]() {
                m_scene->clearSelection();
                custom->setSelected(true);
                m_selectedComponent = custom;
                openEventEditor(custom, "aoClicar");
            });
        } else if (category == "digital_actuator") {
            QAction* act = menu.addAction("Evento: Ao Ligar LED (aoLigar)");
            connect(act, &QAction::triggered, this, [this, custom]() {
                m_scene->clearSelection();
                custom->setSelected(true);
                m_selectedComponent = custom;
                openEventEditor(custom, "aoLigar");
            });
        } else if (category == "analog_input") {
            QAction* actGiro = menu.addAction("Evento: Ao Girar (aoGirar)");
            connect(actGiro, &QAction::triggered, this, [this, custom]() {
                m_scene->clearSelection();
                custom->setSelected(true);
                m_selectedComponent = custom;
                openEventEditor(custom, "aoGirar");
            });

            QAction* actEdit = menu.addAction("Ajustar Rotação...");
            connect(actEdit, &QAction::triggered, this, [this, custom]() {
                this->editCustomPotentiometerValue(custom);
            });
        } else if (category == "active_actuator") {
            QAction* act = menu.addAction("Evento: Ao Tocar (aoTocar)");
            connect(act, &QAction::triggered, this, [this, custom]() {
                m_scene->clearSelection();
                custom->setSelected(true);
                m_selectedComponent = custom;
                openEventEditor(custom, "aoTocar");
            });
        }

        // Add custom events defined by the user
        for (const auto& ev : custom->definition().customEvents) {
            QAction* act = menu.addAction(QString("Evento: %1 (%2)").arg(ev.name).arg(ev.callback));
            connect(act, &QAction::triggered, this, [this, custom, ev]() {
                m_scene->clearSelection();
                custom->setSelected(true);
                m_selectedComponent = custom;
                openEventEditor(custom, ev.callback);
            });
        }

        // Add force simulator option if the simulation is currently active
        if (m_simulator->isRunning()) {
            for (const auto& ev : custom->definition().customEvents) {
                QAction* actForce = menu.addAction(QString("Simulador: Forçar Evento '%1'").arg(ev.name));
                connect(actForce, &QAction::triggered, this, [this, custom, ev]() {
                    m_simulator->triggerComponentEvent(custom->id(), ev.callback);
                });
            }
        }
    } else if (comp->componentType() == "esp32" || comp->componentType() == "board" || comp->componentType() == "generic") {
        QString boardName = "Microcontrolador";
        QVariant cfgVar = comp->property("microcontrollerConfig");
        if (cfgVar.isValid() && cfgVar.canConvert<QString>()) {
            QJsonDocument doc = QJsonDocument::fromJson(cfgVar.toString().toUtf8());
            if (doc.isObject()) {
                boardName = doc.object().value("board").toString().toUpper();
            }
        }

        QAction* actStart = menu.addAction("Evento: Ao Iniciar (aoIniciar)");
        connect(actStart, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoIniciar");
        });

        QAction* actLoop = menu.addAction("Evento: Loop Principal (aoLoop)");
        connect(actLoop, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoLoop");
        });

        menu.addSeparator();

        // ── Trocar Microcontrolador submenu ──────────────────────────────
        QMenu* swapMenu = menu.addMenu(QString("Trocar %1 por...").arg(boardName));
        swapMenu->setStyleSheet(menu.styleSheet());

        // Helper lambda that builds a config and applies it
        auto applyBoardCfg = [this, comp](const QString& boardId, const QString& label,
                                           double widthMm, double heightMm,
                                           double pinWidthMm, double pinHeightMm,
                                           double pinPitchMm,
                                           const QJsonArray& pins)
        {
            QJsonObject cfg;
            cfg["board"] = boardId;
            cfg["core"] = "arduino";
            cfg["upload_port"] = "Auto-Detect";
            cfg["upload_speed"] = "Auto";
            cfg["pin_pitch_mm"] = pinPitchMm;
            QJsonObject bs; 
            bs["width_mm"] = widthMm; 
            bs["height_mm"] = heightMm;
            bs["pin_width_mm"] = pinWidthMm;
            bs["pin_height_mm"] = pinHeightMm;
            cfg["board_size"] = bs;
            cfg["pins"] = pins;
            comp->setProperty("microcontrollerConfig",
                QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact)));
            comp->applyMicrocontrollerConfig(cfg);
            // Update visual label stored on the item
            m_scene->updateCablePaths();
            update();
            logMessage(QString("Microcontrolador alterado para: %1").arg(label), "SUCCESS");
        };

        // ── Option 1: ESP32-C3 Mini (padrão) ──────────
        QAction* actDevKit = swapMenu->addAction("ESP32-C3 Mini (padrão)");
        connect(actDevKit, &QAction::triggered, this, [applyBoardCfg]() {
            QJsonArray pins;
            // Left side (8 pins)
            pins.append(QJsonObject{{"name", "5V"},     {"pin", "5V"},   {"role", "5V"},     {"side", "left"}, {"position", 0}});
            pins.append(QJsonObject{{"name", "GND.1"},  {"pin", "GND"},  {"role", "GND"},    {"side", "left"}, {"position", 1}});
            pins.append(QJsonObject{{"name", "3V3"},    {"pin", "3V3"},  {"role", "3V3"},    {"side", "left"}, {"position", 2}});
            pins.append(QJsonObject{{"name", "GPIO4"},  {"pin", "4"},    {"role", "GPIO4"},  {"side", "left"}, {"position", 3}});
            pins.append(QJsonObject{{"name", "GPIO3"},  {"pin", "3"},    {"role", "GPIO3"},  {"side", "left"}, {"position", 4}});
            pins.append(QJsonObject{{"name", "GPIO2"},  {"pin", "2"},    {"role", "GPIO2"},  {"side", "left"}, {"position", 5}});
            pins.append(QJsonObject{{"name", "GPIO1"},  {"pin", "1"},    {"role", "GPIO1"},  {"side", "left"}, {"position", 6}});
            pins.append(QJsonObject{{"name", "GPIO0"},  {"pin", "0"},    {"role", "GPIO0"},  {"side", "left"}, {"position", 7}});

            // Right side (8 pins)
            pins.append(QJsonObject{{"name", "GPIO5"},  {"pin", "5"},    {"role", "GPIO5"},  {"side", "right"}, {"position", 0}});
            pins.append(QJsonObject{{"name", "GPIO6"},  {"pin", "6"},    {"role", "GPIO6"},  {"side", "right"}, {"position", 1}});
            pins.append(QJsonObject{{"name", "GPIO7"},  {"pin", "7"},    {"role", "GPIO7"},  {"side", "right"}, {"position", 2}});
            pins.append(QJsonObject{{"name", "GPIO8"},  {"pin", "8"},    {"role", "GPIO8"},  {"side", "right"}, {"position", 3}});
            pins.append(QJsonObject{{"name", "GPIO9"},  {"pin", "9"},    {"role", "GPIO9"},  {"side", "right"}, {"position", 4}});
            pins.append(QJsonObject{{"name", "GPIO10"}, {"pin", "10"},   {"role", "GPIO10"}, {"side", "right"}, {"position", 5}});
            pins.append(QJsonObject{{"name", "GPIO20"}, {"pin", "20"},   {"role", "GPIO20"}, {"side", "right"}, {"position", 6}});
            pins.append(QJsonObject{{"name", "GPIO21"}, {"pin", "21"},   {"role", "GPIO21"}, {"side", "right"}, {"position", 7}});

            applyBoardCfg("esp32-c3-devkitm-1", "ESP32-C3 Mini", 18.0, 22.52, 15.24, 22.52, 2.54, pins);
        });

        // ── Option 2: ESP32-C3-WROOM-02 (Módulo) ───────────────────────────
        QAction* actWroom02 = swapMenu->addAction("ESP32-C3-WROOM-02 (Módulo)");
        connect(actWroom02, &QAction::triggered, this, [applyBoardCfg]() {
            QJsonArray pins;
            // Left side (9 pins) top to bottom
            pins.append(QJsonObject{{"name","3V3"},   {"pin","3V3"},  {"role","3V3"},    {"side","left"}, {"position",0}});
            pins.append(QJsonObject{{"name","EN"},    {"pin","EN"},   {"role","EN"},     {"side","left"}, {"position",1}});
            pins.append(QJsonObject{{"name","IO4"},   {"pin","4"},    {"role","IO4"},    {"side","left"}, {"position",2}});
            pins.append(QJsonObject{{"name","IO5"},   {"pin","5"},    {"role","IO5"},    {"side","left"}, {"position",3}});
            pins.append(QJsonObject{{"name","IO6"},   {"pin","6"},    {"role","IO6"},    {"side","left"}, {"position",4}});
            pins.append(QJsonObject{{"name","IO7"},   {"pin","7"},    {"role","IO7"},    {"side","left"}, {"position",5}});
            pins.append(QJsonObject{{"name","IO8"},   {"pin","8"},    {"role","IO8"},    {"side","left"}, {"position",6}});
            pins.append(QJsonObject{{"name","IO9"},   {"pin","9"},    {"role","IO9"},    {"side","left"}, {"position",7}});
            pins.append(QJsonObject{{"name","GND"},   {"pin","GND"},  {"role","GND"},    {"side","left"}, {"position",8}});

            // Right side (9 pins) top to bottom
            pins.append(QJsonObject{{"name","IO10"},  {"pin","10"},   {"role","IO10"},   {"side","right"}, {"position",0}});
            pins.append(QJsonObject{{"name","RXD"},   {"pin","20"},   {"role","RXD"},    {"side","right"}, {"position",1}});
            pins.append(QJsonObject{{"name","TXD"},   {"pin","21"},   {"role","TXD"},    {"side","right"}, {"position",2}});
            pins.append(QJsonObject{{"name","IO18"},  {"pin","18"},   {"role","IO18"},   {"side","right"}, {"position",3}});
            pins.append(QJsonObject{{"name","IO19"},  {"pin","19"},   {"role","IO19"},   {"side","right"}, {"position",4}});
            pins.append(QJsonObject{{"name","IO3"},   {"pin","3"},    {"role","IO3"},    {"side","right"}, {"position",5}});
            pins.append(QJsonObject{{"name","IO2"},   {"pin","2"},    {"role","IO2"},    {"side","right"}, {"position",6}});
            pins.append(QJsonObject{{"name","IO1"},   {"pin","1"},    {"role","IO1"},    {"side","right"}, {"position",7}});
            pins.append(QJsonObject{{"name","IO0"},   {"pin","0"},    {"role","IO0"},    {"side","right"}, {"position",8}});

            // Bottom side (9 pins: thermal pad GND vias in 3x3 grid)
            for (int i = 1; i <= 9; ++i) {
                pins.append(QJsonObject{{"name", QString("GND.EPAD%1").arg(i)}, {"pin", "GND"}, {"role", "GND"}, {"side", "bottom"}, {"position", i - 1}});
            }

            applyBoardCfg("esp32-c3-wroom-02", "ESP32-C3-WROOM-02", 18.0, 20.0, 17.5, 20.0, 1.5, pins);
        });

        menu.addSeparator();

        QAction* editMC = menu.addAction("Editar Microcontrolador...");
        connect(editMC, &QAction::triggered, this, [this, comp]() {
            editMicrocontroller(comp);
        });

        menu.addSeparator();

        QAction* actClearEeprom = menu.addAction("Limpar EEPROM");
        connect(actClearEeprom, &QAction::triggered, this, [this]() {
            auto res = QMessageBox::question(this, "Limpar EEPROM",
                "Tem certeza que deseja apagar todos os dados salvos na EEPROM simulada?\n\n"
                "Esta ação não pode ser desfeita.",
                QMessageBox::Yes | QMessageBox::No);
            if (res == QMessageBox::Yes) {
                m_simulator->clearEeprom();
                logMessage("EEPROM simulada limpa com sucesso.", "SUCCESS");
                statusBar()->showMessage("EEPROM limpa!", 3000);
            }
        });
    } else if (comp->componentType() == "led" || comp->componentType() == "rgb_led") {
        QAction* act = menu.addAction("Evento: Ao Ligar LED (aoLigar)");
        connect(act, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoLigar");
        });

        QAction* actEdit = menu.addAction("Editar LED...");
        connect(actEdit, &QAction::triggered, this, [this, comp]() {
            auto* led = static_cast<LEDItem*>(comp);
            this->editLEDProperties(led);
        });
    } else if (comp->componentType() == "resistor") {
        QAction* actEdit = menu.addAction("Editar Resistência...");
        connect(actEdit, &QAction::triggered, this, [this, comp]() {
            auto* res = static_cast<ResistorItem*>(comp);
            this->editResistorValue(res);
        });
    } else if (comp->componentType() == "capacitor") {
        QAction* actEdit = menu.addAction("Editar Capacitor...");
        connect(actEdit, &QAction::triggered, this, [this, comp]() {
            auto* cap = static_cast<CapacitorItem*>(comp);
            this->editCapacitorProperties(cap);
        });
    } else if (comp->componentType() == "potentiometer") {
        QAction* actGiro = menu.addAction("Evento: Ao Girar (aoGirar)");
        connect(actGiro, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoGirar");
        });

        QAction* actEdit = menu.addAction("Ajustar Rotação...");
        connect(actEdit, &QAction::triggered, this, [this, comp]() {
            auto* pot = static_cast<PotentiometerItem*>(comp);
            this->editPotentiometerValue(pot);
        });
    } else if (comp->componentType() == "ldr") {
        QAction* actAlterar = menu.addAction("Evento: Ao Alterar (aoAlterar)");
        connect(actAlterar, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoAlterar");
        });

        QAction* actEdit = menu.addAction("Ajustar Nível de Luz...");
        connect(actEdit, &QAction::triggered, this, [this, comp]() {
            auto* ldr = static_cast<LdrItem*>(comp);
            this->editLdrValue(ldr);
        });
    } else if (comp->componentType() == "buzzer") {
        QAction* act = menu.addAction("Evento: Ao Tocar (aoTocar)");
        connect(act, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoTocar");
        });
    } else if (comp->componentType() == "motor") {
        QAction* actEdit = menu.addAction("Editar Motor...");
        connect(actEdit, &QAction::triggered, this, [this, comp]() {
            auto* motor = static_cast<MotorItem*>(comp);
            this->editMotorProperties(motor);
        });
    } else if (comp->componentType() == "dht22") {
        QAction* actHum = menu.addAction("Evento: Ao Calcular Umidade (aoCalcularUmidade)");
        connect(actHum, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoCalcularUmidade");
        });
        QAction* actTemp = menu.addAction("Evento: Ao Calcular Temperatura (aoCalcularTemperatura)");
        connect(actTemp, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoCalcularTemperatura");
        });
    } else if (comp->componentType() == "hcsr04") {
        QAction* actMedir = menu.addAction("Evento: Ao Medir (aoMedir)");
        connect(actMedir, &QAction::triggered, this, [this, comp]() {
            m_scene->clearSelection();
            comp->setSelected(true);
            m_selectedComponent = comp;
            openEventEditor(comp, "aoMedir");
        });
    }

    if (!menu.actions().isEmpty()) {
        menu.addSeparator();
    }
    
    QAction* renameAct = menu.addAction("Renomear Componente");
    connect(renameAct, &QAction::triggered, this, [this, comp]() {
        bool ok;
        QString newName = QInputDialog::getText(this, "Renomear Componente",
                                                "Novo nome:", QLineEdit::Normal,
                                                comp->name(), &ok);
        if (ok && !newName.isEmpty() && newName != comp->name()) {
            m_scene->undoStack()->push(new RenameComponentCommand(comp, comp->name(), newName));
            logMessage(QString("Componente renomeado para: %1").arg(newName), "INFO");
        }
    });

    QAction* rotateAct = menu.addAction("Girar 90 graus");
    connect(rotateAct, &QAction::triggered, this, [this, comp]() {
        comp->setRotation(comp->rotation() + 90.0);
        if (comp->rotation() >= 360.0) comp->setRotation(0);
        m_scene->updateCablePaths();
    });

    menu.exec(QPoint(globalPos.x(), globalPos.y()));
}

void MainWindow::toggleSimulation() {
    auto* playAction = qobject_cast<QAction*>(sender());
    if (!playAction) return;

    if (m_simulator->isRunning() || (m_nativeSimProcess && m_nativeSimProcess->state() == QProcess::Running)) {
        m_simulator->stopSimulation();
        if (m_nativeSimProcess) {
            m_nativeSimProcess->kill();
            m_nativeSimProcess->waitForFinished();
        }
        m_scene->setSimulating(false);
        
        // Unlock UI
        m_toolboxList->setEnabled(true);
        if (m_saveAction) m_saveAction->setEnabled(true);
        if (m_newAction)  m_newAction->setEnabled(true);
        if (m_openAction) m_openAction->setEnabled(true);
        if (m_clearAction) m_clearAction->setEnabled(true);
        if (m_buildAction) m_buildAction->setEnabled(true);
        if (m_undoAction)  m_undoAction->setEnabled(true);
        if (m_redoAction)  m_redoAction->setEnabled(true);

        updatePlayActionState();
        playAction->setChecked(false);
        statusBar()->showMessage("Simulação finalizada");
        logMessage("Simulação interativa finalizada.", "SYSTEM");
        
        // Reconnect normal clicks
        for (auto* comp : m_scene->components()) {
            if (comp->componentType() == "button") {
                auto* btn = static_cast<ButtonItem*>(comp);
                btn->disconnect(this);
            } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                if (custom->category() == "digital_trigger") {
                    custom->disconnect(this);
                }
            }
        }
    } else {
        bool useNative = false;
        if (!m_lastBuildOk) {
            auto resp = QMessageBox::question(this, "Simulação Rápida", 
                "O projeto não foi construído (Build).\nDeseja fazer uma simulação rápida (sem verificação de erros de roteamento)?",
                QMessageBox::Yes | QMessageBox::No);
            if (resp == QMessageBox::No) {
                playAction->setChecked(false);
                return;
            }
        } else {
            useNative = true;
        }
        
        statusBar()->showMessage("Iniciando Simulacao...");
        
        // Notify oscilloscope before starting
        m_oscilloscope->onSimulationStarted();
        // Switch to oscilloscope tab automatically when simulation starts
        if (m_bottomTabs) m_bottomTabs->setCurrentIndex(1);

        if (useNative) {
            logMessage("Simulação Nativa em C++: Compilando...", "SYSTEM");
            ToolchainManager(this).preparePlatformIOProject(true);
            
            QDir buildDir(qApp->applicationDirPath());
            QString pioPath = buildDir.filePath("pio_project");
            
            if (!m_nativeSimProcess) {
                m_nativeSimProcess = new QProcess(this);
                connect(m_nativeSimProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::readNativeSimOutput);
            }
            
            // Compila o executável nativo
            QProcess compileProc;
            QString pioCmd = ToolchainManager(this).getPlatformIOCommand();
            if (pioCmd.isEmpty()) pioCmd = "platformio";
            compileProc.setProgram(pioCmd);
            compileProc.setArguments({"run", "-e", "native"});
            compileProc.setWorkingDirectory(pioPath);
            compileProc.start();
            compileProc.waitForFinished();
            
            if (compileProc.exitCode() != 0) {
                logMessage("Falha ao compilar simulação nativa! Verificando erros...", "ERROR");
                logMessage(QString::fromUtf8(compileProc.readAllStandardError()), "ERROR");
                playAction->setChecked(false);
                return;
            }
            
            // Build the Pin to Component map
            m_scene->setProperty("nativePinMap", QVariant());
            QMap<int, QString> pinToCompId;
            QRegularExpression re("#define PIN_([A-Za-z0-9_]+)\\s+(\\d+)");
            QRegularExpressionMatchIterator i = re.globalMatch(m_compiledCode);
            QMap<QString, int> nameToPin;
            while (i.hasNext()) {
                QRegularExpressionMatch match = i.next();
                nameToPin[match.captured(1)] = match.captured(2).toInt();
            }
            for (auto* comp : m_scene->components()) {
                QString sanitized = comp->name().toLower().replace(" ", "_");
                sanitized.remove(QRegularExpression("[^a-z0-9_]"));
                if (nameToPin.contains(sanitized)) {
                    pinToCompId[nameToPin[sanitized]] = comp->id();
                }
            }
            
            QVariantMap vmap;
            for (auto it = pinToCompId.begin(); it != pinToCompId.end(); ++it) {
                vmap[QString::number(it.key())] = it.value();
            }
            m_scene->setProperty("nativePinMap", vmap);
            
            QString exePath = pioPath + "/.pio/build/native/program.exe";
#ifdef Q_OS_WIN
            if (!QFile::exists(exePath)) exePath = pioPath + "/.pio/build/native/program.exe";
#else
            exePath = pioPath + "/.pio/build/native/program";
#endif
            
            m_nativeSimProcess->start(exePath);
            logMessage("Simulação Nativa em execução!", "SUCCESS");
            m_scene->setSimulating(true);
            
            // Send button clicks directly to stdin
            for (auto* comp : m_scene->components()) {
                if (comp->componentType() == "button") {
                    auto* btn = static_cast<ButtonItem*>(comp);
                    connect(btn, &ButtonItem::stateChanged, this, [this, btn, pinToCompId](bool pressed) {
                        int pinNum = -1;
                        for (auto it = pinToCompId.begin(); it != pinToCompId.end(); ++it) {
                            if (it.value() == btn->id()) { pinNum = it.key(); break; }
                        }
                        if (pinNum >= 0 && m_nativeSimProcess) {
                            QString cmd = QString("SET:%1:%2\n").arg(pinNum).arg(pressed ? 1 : 0);
                            m_nativeSimProcess->write(cmd.toUtf8());
                        }
                    });
                }
            }
            
        } else {
            logMessage("Simulacao Rápida (Interpretador) iniciada.", "SYSTEM");
            logMessage("Dica: Clique nos botoes pulsadores e ajuste os potenciometros para ver os eventos disparando em tempo real!", "INFO");
            
            m_simulator->setMultitaskingEnabled(m_multitaskingEnabled);
            m_simulator->startSimulation(m_scene, m_blockEditor->getEventBlockStorage(), m_webPageData);
            
            // Hide block editor when simulation starts
            if (m_blockEditor) {
                m_blockEditor->hide();
                m_blockEditor->setEnabled(false);
            }
            
            // Fix: Actually set the scene to simulating mode so components know!
            m_scene->setSimulating(true);
            
            // Hook visual button toggles to simulator triggers
            for (auto* comp : m_scene->components()) {
                if (comp->componentType() == "button") {
                    auto* btn = static_cast<ButtonItem*>(comp);
                    connect(btn, &ButtonItem::stateChanged, this, [this, btn](bool pressed) {
                        if (pressed) {
                            // Fire both legacy aoClicar and new aoPressionar
                            m_simulator->triggerComponentEvent(btn->id(), "aoClicar");
                            m_simulator->triggerComponentEvent(btn->id(), "aoPressionar");
                        } else {
                            m_simulator->triggerComponentEvent(btn->id(), "aoSoltar");
                        }
                    });
                } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                    if (custom->category() == "digital_trigger") {
                        connect(custom, &CustomComponentItem::stateChanged, this, [this, custom](bool pressed) {
                            if (pressed) {
                                m_simulator->triggerComponentEvent(custom->id(), "aoClicar");
                            }
                        });
                    }
                }
            }
        }

        playAction->setIcon(QIcon(":/icons/stop.svg"));
        playAction->setChecked(true);
        statusBar()->showMessage("Simulação Ativa! Servidor Web operando em localhost:8080");
        
        if (m_webPageData.value("enabled").toBool()) {
            QDesktopServices::openUrl(QUrl("http://localhost:8080"));
        }
    }
}



void MainWindow::compileCode() {
    synchronizeLoopBlocks();
    // Generate valid C++ firmware script matching pins and connections
    m_compiledCode = CodeGenerator::generateArduinoCode(
        m_scene->components(),
        m_scene->cables(),
        m_blockEditor->getEventBlockStorage(),
        m_webPageData
    );

    if (m_compiledCode.startsWith("// ERROR:")) {
        m_lastBuildOk = false; updatePlayActionState();
        QString errorMsg = m_compiledCode.mid(QString("// ERROR:").length()).trimmed();
        logMessage("Erro de Roteamento/Alimentação: " + errorMsg, "ERROR");
        statusBar()->showMessage("Erro de Conexão no Projeto!", 5000);
        return;
    }

    logMessage("Firmware C++ / Arduino gerado com sucesso em segundo plano.", "SUCCESS");
    statusBar()->showMessage("Firmware C++ atualizado!", 3000);
}

void MainWindow::buildProject() {
    if (m_isBuilding) {
        logMessage("Uma compilação já está em andamento. Aguarde.", "WARNING");
        return;
    }
    m_isBuilding = true;
    m_buildAction->setEnabled(false);
    
    logMessage("Iniciando compilação de hardware (isso pode congelar a interface por alguns instantes)...", "INFO");
    QCoreApplication::processEvents();

    auto buildGuard = qScopeGuard([this] {
        m_isBuilding = false;
        m_buildAction->setEnabled(true);
    });

    // 1) Validate microcontroller configuration
    if (!ToolchainManager(this).isMicrocontrollerConfigured()) {
        auto resp = QMessageBox::question(this, "Configuração Necessária",
            "A placa ou framework do microcontrolador ainda não foram configurados.\nDeseja realizar essa configuração agora?",
            QMessageBox::Yes | QMessageBox::No);
        if (resp == QMessageBox::Yes) {
            QString b, f, p, s;
            if (!ToolchainManager(this).showPlatformIOConfigDialog(b, f, p, s)) {
                logMessage("Build cancelado pelo usuário (configuração necessária).", "WARNING");
                return;
            }
        } else {
            logMessage("Build abortado: microcontrolador não configurado.", "ERROR");
            return;
        }
    }

    // 2) Generate code
    compileCode();

    if (m_aiOptimizationAction && m_aiOptimizationAction->isChecked()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Compilar Otimizado?");
        msgBox.setText("O Modo Copilot está ativado. Deseja compilar a versão Padrão gerada pelos blocos, ou pedir para a IA Otimizar o código antes de gravar?");
        auto btnOtimizado = msgBox.addButton("Compilar Otimizado", QMessageBox::AcceptRole);
        auto btnPadrao = msgBox.addButton("Compilar Padrão", QMessageBox::RejectRole);
        msgBox.exec();

        if (msgBox.clickedButton() == btnOtimizado) {
            logMessage("Otimizando código com IA (Copilot)... aguarde...", "SYSTEM");
            QCoreApplication::processEvents();
            
            QEventLoop loop;
            QString optResult = "";
            bool success = false;
            
            auto conn1 = connect(m_aiOptimizer, &AiOptimizer::optimizationFinished, [&](const QString& optCode) {
                optResult = optCode;
                success = true;
                loop.quit();
            });
            auto conn2 = connect(m_aiOptimizer, &AiOptimizer::optimizationError, [&](const QString& err) {
                logMessage("Erro do Copilot: " + err, "ERROR");
                loop.quit();
            });
            
            m_aiOptimizer->optimizeCode(m_compiledCode, AiOptimizer::OptimizePerformance);
            loop.exec();
            
            disconnect(conn1);
            disconnect(conn2);
            
            if (success && !optResult.isEmpty()) {
                m_compiledCode = optResult;
                logMessage("Código otimizado pela IA com sucesso!", "SYSTEM");
            } else {
                logMessage("Falha na otimização. Prosseguindo com código Padrão.", "WARNING");
            }
        }
    }

    if (m_compiledCode.startsWith("// ERROR:")) {
        QString errorMsg = m_compiledCode.mid(QString("// ERROR:").length()).trimmed();
        QMessageBox::critical(this, "Erro de Conexão (Alimentação/GND)", errorMsg);
        m_compiledCode = "";
        m_lastBuildOk = false; updatePlayActionState();
        return;
    }

    // 3) Quick static checks
    if (m_compiledCode.trimmed().isEmpty()) {
        logMessage("Build falhou: código gerado está vazio.", "ERROR");
        QMessageBox::warning(this, "Build falhou", "Código gerado está vazio. Verifique o projeto.");
        m_lastBuildOk = false; updatePlayActionState();
        return;
    }

    // Simple brace balance check
    int opens = m_compiledCode.count('{');
    int closes = m_compiledCode.count('}');
    if (opens != closes) {
        logMessage("Build falhou: chaves não balanceadas no código gerado.", "ERROR");
        QMessageBox::warning(this, "Build falhou", "Erro de sintaxe detectado: chaves não balanceadas.");
        m_lastBuildOk = false; updatePlayActionState();
        if (m_playAction) m_playAction->setEnabled(false);
        return;
    }

    // Detect common warnings/messages from generator indicating missing hardware
    if (m_compiledCode.contains("Nenhum ESP32") || m_compiledCode.contains("ATENÇÃO:") || m_compiledCode.contains("AVISO:")) {
        logMessage("Build terminou com avisos; verifique as mensagens do gerador.", "WARNING");
    }

    // Optionally write generated code to build folder for inspection
    QString outPath = QDir(qApp->applicationDirPath()).filePath("generated_sketch.ino");
    QFile outFile(outPath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        outFile.write(m_compiledCode.toUtf8());
        outFile.close();
    }

    // 4) PlatformIO Compile Verification
    if (ToolchainManager(this).platformIOIsInstalled()) {
        bool pioOk = ToolchainManager(this).platformIOBuild();
        if (!pioOk) {
            logMessage("Verificação de compilação do PlatformIO falhou! Corrija os erros acima.", "ERROR");
            QMessageBox::warning(this, "Erro de Compilação", "O PlatformIO detectou erros de compilação no código gerado. Verifique os logs no console.");
            m_lastBuildOk = false; updatePlayActionState();
            return;
        }
    } else {
        logMessage("Aviso: PlatformIO não está instalado no sistema. Ignorando a validação de compilação de hardware.", "WARNING");
    }

    m_lastBuildOk = true; updatePlayActionState();
    if (m_playAction) m_playAction->setEnabled(true);
    logMessage("Build concluído com sucesso. Simulação habilitada.", "SUCCESS");
    statusBar()->showMessage("Build concluído com sucesso.", 3000);
}

void MainWindow::clearScene() {
    // Block signals to avoid re-entrant slots (e.g. blocksChanged -> compileCode)
    if (m_blockEditor) {
        bool prevBlockSignals = m_blockEditor->signalsBlocked();
        m_blockEditor->blockSignals(true);
        m_blockEditor->clearAllBlocks();
        m_blockEditor->blockSignals(prevBlockSignals);
    }
    if (m_scene) {
        bool prevSceneSignals = m_scene->signalsBlocked();
        m_scene->blockSignals(true);
        m_scene->clearWorkspace();
        m_scene->addComponent("esp32", "Controlador ESP32", QPointF(0, 0));
        m_scene->blockSignals(prevSceneSignals);
    }
    m_selectedComponent = nullptr;
    m_simulator->clearEeprom();
    compileCode();
    logMessage("Workspace limpo pelo usuário. Todos os componentes e cabos foram removidos.", "WARNING");
    statusBar()->showMessage("Workspace limpo!");
}

void MainWindow::newProject() {
    clearScene();
    m_currentProjectPath.clear();
    m_webPageData = QJsonObject();
    statusBar()->showMessage("Novo projeto iniciado.", 2500);
    logMessage("Novo projeto criado.", "SYSTEM");
}


void MainWindow::openProject() {
    const QString filePath = QFileDialog::getOpenFileName(this,
        "Abrir Projeto",
        QString(),
        "Projeto IDE Embedded (*.ideproject);;JSON (*.json);;Todos os arquivos (*.*)");
    if (filePath.isEmpty()) {
        return;
    }

    if (!ProjectManager(this).loadProjectFromFile(filePath)) {
        QMessageBox::warning(this, "Abrir Projeto", "Não foi possível abrir o projeto selecionado.");
    }
}

void MainWindow::saveProject() {
    if (m_currentProjectPath.isEmpty()) {
        saveProjectAs();
        return;
    }

    if (ProjectManager(this).saveProjectToFile(m_currentProjectPath)) {
        statusBar()->showMessage(QString("Projeto salvo em %1").arg(QFileInfo(m_currentProjectPath).fileName()), 3000);
        logMessage(QString("Projeto salvo: %1").arg(m_currentProjectPath), "SUCCESS");
    } else {
        QMessageBox::warning(this, "Salvar Projeto", "Não foi possível salvar o projeto atual.");
    }
}

void MainWindow::saveProjectAs() {
    QString filePath = QFileDialog::getSaveFileName(this,
        "Salvar Projeto Como",
        m_currentProjectPath.isEmpty() ? QString("projeto.ideproject") : m_currentProjectPath,
        "Projeto IDE Embedded (*.ideproject);;JSON (*.json)");
    if (filePath.isEmpty()) {
        return;
    }

    if (!filePath.endsWith(".ideproject", Qt::CaseInsensitive) && !filePath.endsWith(".json", Qt::CaseInsensitive)) {
        filePath += ".ideproject";
    }

    if (ProjectManager(this).saveProjectToFile(filePath)) {
        m_currentProjectPath = filePath;
        statusBar()->showMessage(QString("Projeto salvo em %1").arg(QFileInfo(filePath).fileName()), 3000);
        logMessage(QString("Projeto salvo como: %1").arg(filePath), "SUCCESS");
    } else {
        QMessageBox::warning(this, "Salvar Projeto", "Não foi possível salvar o projeto.");
    }
}


void MainWindow::exportLaserPNG() {
    // ── Step 1: Configuration dialog ────────────────────────────────────────
    QDialog configDlg(this);
    configDlg.setWindowTitle("Configurar Export Laser");
    configDlg.setMinimumWidth(360);
    configDlg.setStyleSheet(
        "QDialog { background: #F8FAFC; border: 1px solid #CBD5E1; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial; font-size: 11px; font-weight: 600; }"
        "QLabel#title { color: #0F172A; font-size: 15px; font-weight: 700; }"
        "QLabel#sub { color: #475569; font-size: 10px; font-weight: 400; }"
        "QDoubleSpinBox { background: rgba(255,255,255,0.7); border: 1.5px solid rgba(255,255,255,0.8); border-radius: 8px;"
        "  color: #0F172A; padding: 9px 12px; font-size: 13px; font-weight: 600; }"
        "QDoubleSpinBox:focus { border-color: #0EA5E9; background: #FFFFFF; }"
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #93C5FD, stop:0.45 #3B82F6, stop:0.46 #2563EB, stop:1 #1D4ED8); "
        "  border: 1.5px solid #2563EB; "
        "  border-radius: 8px; "
        "  color: white; "
        "  padding: 11px 22px; "
        "  font-weight: 700; "
        "  font-size: 13px; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3B82F6, stop:0.45 #2563EB, stop:0.46 #1D4ED8, stop:1 #1E40AF); "
        "  border-color: #1D4ED8; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1D4ED8, stop:1 #1E3A8A); "
        "} "
        "QPushButton#cancel { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F8FAFC, stop:0.46 #F1F5F9, stop:1 #E2E8F0); "
        "  border: 1.5px solid #CBD5E1; "
        "  border-radius: 8px; "
        "  color: #475569; "
        "  padding: 11px 22px; "
        "  font-weight: 700; "
        "  font-size: 13px; "
        "} "
        "QPushButton#cancel:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FAFC, stop:0.45 #F1F5F9, stop:0.46 #E2E8F0, stop:1 #CBD5E1); "
        "  border-color: #94A3B8; "
        "  color: #0F172A; "
        "} "
        "QPushButton#cancel:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E2E8F0, stop:1 #94A3B8); "
        "}"
    );

    auto* cfgLayout = new QVBoxLayout(&configDlg);
    cfgLayout->setSpacing(14);
    cfgLayout->setContentsMargins(28, 28, 28, 28);

    auto* cfgTitle = new QLabel("Exportar Trilhas para Laser", &configDlg);
    cfgTitle->setObjectName("title");
    cfgLayout->addWidget(cfgTitle);

    auto* cfgSub = new QLabel("Configure os parâmetros e visualize o preview antes de salvar.", &configDlg);
    cfgSub->setObjectName("sub");
    cfgSub->setWordWrap(true);
    cfgLayout->addWidget(cfgSub);

    cfgLayout->addSpacing(6);

    auto* trackLabel = new QLabel("Espessura da trilha de cobre (MIL):", &configDlg);
    cfgLayout->addWidget(trackLabel);
    auto* trackSpin = new QDoubleSpinBox(&configDlg);
    trackSpin->setRange(10.0, 300.0);
    trackSpin->setValue(80.0);
    trackSpin->setSingleStep(10.0);
    trackSpin->setDecimals(1);
    trackSpin->setSuffix(" MIL");
    cfgLayout->addWidget(trackSpin);

    auto* lineLabel = new QLabel("Espessura do isolamento (MIL):", &configDlg);
    cfgLayout->addWidget(lineLabel);
    auto* lineSpin = new QDoubleSpinBox(&configDlg);
    lineSpin->setRange(1.0, 50.0);
    lineSpin->setValue(20.0);
    lineSpin->setSingleStep(1.0);
    lineSpin->setDecimals(1);
    lineSpin->setSuffix(" MIL");
    cfgLayout->addWidget(lineSpin);

    auto* infoLabel = new QLabel(&configDlg);
    infoLabel->setObjectName("sub");
    cfgLayout->addWidget(infoLabel);

    auto updateInfo = [=]() {
        double tm = trackSpin->value() * 0.0254;
        double lm = lineSpin->value() * 0.0254;
        infoLabel->setText(QString("Trilha: %1 mm  |  Isolamento: %2 mm  (100 MIL = 2.54 mm)")
                               .arg(tm, 0, 'f', 2).arg(lm, 0, 'f', 2));
    };
    connect(trackSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), updateInfo);
    connect(lineSpin,  qOverload<double>(&QDoubleSpinBox::valueChanged), updateInfo);
    updateInfo();

    auto* cfgBtnRow = new QHBoxLayout();
    cfgBtnRow->setSpacing(12);
    cfgBtnRow->setContentsMargins(0, 8, 0, 0);
    auto* cfgCancel = new QPushButton("Cancelar", &configDlg);
    cfgCancel->setObjectName("cancel");
    auto* cfgPreview = new QPushButton("Ver Preview", &configDlg);
    cfgBtnRow->addWidget(cfgCancel);
    cfgBtnRow->addWidget(cfgPreview);
    cfgLayout->addLayout(cfgBtnRow);

    connect(cfgPreview, &QPushButton::clicked, &configDlg, &QDialog::accept);
    connect(cfgCancel,  &QPushButton::clicked, &configDlg, &QDialog::reject);

    if (configDlg.exec() != QDialog::Accepted) return;

    double trackWidthMil = trackSpin->value();
    double lineWidthMil  = lineSpin->value();

    // ── Step 2: Generate preview image ──────────────────────────────────────
    logMessage("Gerando preview do layout laser...", "INFO");
    QImage previewImage = PcbExporter::generateLaserImage(
        m_scene->components(), m_scene->cables(), trackWidthMil, lineWidthMil);

    if (previewImage.isNull()) {
        logMessage("Falha ao gerar preview — workspace sem componentes.", "ERROR");
        QMessageBox::warning(this, "Preview", "Nenhum componente no workspace para exportar.");
        return;
    }

    // ── Step 3: Preview dialog ───────────────────────────────────────────────
    QDialog previewDlg(this);
    previewDlg.setWindowTitle("Preview — Layout para Laser");
    previewDlg.resize(980, 720);
    previewDlg.setStyleSheet(
        "QDialog { background: #F8FAFC; border: 1px solid #CBD5E1; }"
        "QLabel#header { color: #0F172A; font-size: 15px; font-weight: 700;"
        "  font-family: 'Segoe UI', Arial; padding: 0 0 4px 0; }"
        "QLabel#meta { color: #475569; font-size: 11px; font-family: 'Segoe UI', Arial; }"
        "QLabel#badge { background: rgba(255,255,255,0.6); border: 1px solid rgba(255,255,255,0.8); border-radius: 6px;"
        "  color: #0369A1; font-size: 11px; font-family: 'Segoe UI', Arial;"
        "  padding: 4px 10px; font-weight: bold; }"
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #93C5FD, stop:0.45 #3B82F6, stop:0.46 #2563EB, stop:1 #1D4ED8); "
        "  border: 1.5px solid #2563EB; "
        "  border-radius: 8px; "
        "  color: white; "
        "  padding: 11px 24px; "
        "  font-weight: 700; "
        "  font-size: 13px; "
        "  font-family: 'Segoe UI', Arial; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3B82F6, stop:0.45 #2563EB, stop:0.46 #1D4ED8, stop:1 #1E40AF); "
        "  border-color: #1D4ED8; "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1D4ED8, stop:1 #1E3A8A); "
        "} "
        "QPushButton#discard { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F8FAFC, stop:0.46 #F1F5F9, stop:1 #E2E8F0); "
        "  border: 1.5px solid #CBD5E1; "
        "  color: #475569; "
        "} "
        "QPushButton#discard:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FAFC, stop:0.45 #F1F5F9, stop:0.46 #E2E8F0, stop:1 #CBD5E1); "
        "  border-color: #94A3B8; "
        "  color: #0F172A; "
        "} "
        "QPushButton#discard:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E2E8F0, stop:1 #94A3B8); "
        "} "
        "QPushButton#toolBtn { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F8FAFC, stop:0.46 #F1F5F9, stop:1 #E2E8F0); "
        "  border: 1.5px solid #CBD5E1; "
        "  color: #334155; "
        "  padding: 8px 12px; "
        "  font-weight: 600; "
        "  font-size: 12px; "
        "  border-radius: 6px; "
        "  font-family: 'Segoe UI', Arial; "
        "} "
        "QPushButton#toolBtn:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FAFC, stop:0.45 #F1F5F9, stop:0.46 #E2E8F0, stop:1 #CBD5E1); "
        "  border-color: #94A3B8; "
        "} "
        "QPushButton#toolBtn:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E2E8F0, stop:1 #94A3B8); "
        "} "
        "QPushButton#toolBtn:checked { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFF1F2, stop:1 #FFE4E6); "
        "  border-color: #FECDD3; "
        "  color: #E11D48; "
        "} "
        "QScrollArea { border: 1.5px solid rgba(255,255,255,0.8); background: rgba(255,255,255,0.4); border-radius: 10px; }"
    );

    auto* pvLayout = new QVBoxLayout(&previewDlg);
    pvLayout->setSpacing(0);
    pvLayout->setContentsMargins(24, 20, 24, 20);

    // Header
    auto* pvHeader = new QLabel("Preview do Layout PCB", &previewDlg);
    pvHeader->setObjectName("header");
    pvLayout->addWidget(pvHeader);

    // Meta badges row
    auto* metaRow = new QHBoxLayout();
    metaRow->setSpacing(8);
    metaRow->setContentsMargins(0, 6, 0, 12);

    int compCount  = m_scene->components().size();
    int cableCount = m_scene->cables().size();
    double widthMm  = previewImage.width()  / (5.0 * (10.0 / 2.54)); // 5x render
    double heightMm = previewImage.height() / (5.0 * (10.0 / 2.54));

    auto makeBadge = [&](const QString& text) -> QLabel* {
        auto* b = new QLabel(text, &previewDlg);
        b->setObjectName("badge");
        return b;
    };
    metaRow->addWidget(makeBadge(QString("%1 componentes").arg(compCount)));
    metaRow->addWidget(makeBadge(QString("%1 cabos").arg(cableCount)));
    metaRow->addWidget(makeBadge(QString("%1 × %2 mm").arg(widthMm, 0, 'f', 1).arg(heightMm, 0, 'f', 1)));
    metaRow->addWidget(makeBadge(QString("Trilha: %1 MIL  |  Iso: %2 MIL")
                                     .arg(trackWidthMil, 0, 'f', 0).arg(lineWidthMil, 0, 'f', 0)));
    metaRow->addStretch();
    pvLayout->addLayout(metaRow);

    // Main horizontal layout for Sidebar + Preview
    auto* mainHLayout = new QHBoxLayout();
    mainHLayout->setSpacing(18);
    
    // Left Control Panel (Sidebar)
    auto* ctrlFrame = new QFrame(&previewDlg);
    ctrlFrame->setObjectName("ctrlFrame");
    ctrlFrame->setFixedWidth(230);
    ctrlFrame->setStyleSheet(
        "QFrame#ctrlFrame { background: #F8FAFC; border: 1px solid #E2E8F0; border-radius: 10px; }"
        "QCheckBox { color: #334155; font-size: 12px; font-weight: 600; font-family: 'Segoe UI', Arial; padding: 4px; }"
        "QCheckBox:hover { color: #0F172A; }"
        "QLabel#sectionTitle { color: #0F172A; font-size: 11px; font-weight: 700; font-family: 'Segoe UI', Arial; text-transform: uppercase; letter-spacing: 0.5px; margin-top: 10px; }"
    );
    
    auto* ctrlLayout = new QVBoxLayout(ctrlFrame);
    ctrlLayout->setContentsMargins(16, 16, 16, 16);
    ctrlLayout->setSpacing(12);
    
    // Section: Layers
    auto* layersTitle = new QLabel("Camadas Visualizadas", ctrlFrame);
    layersTitle->setObjectName("sectionTitle");
    ctrlLayout->addWidget(layersTitle);
    
    auto* cbTracks = new QCheckBox("Trilhas e Ilhas (Cobre)", ctrlFrame);
    cbTracks->setChecked(true);
    ctrlLayout->addWidget(cbTracks);
    
    auto* cbDrills = new QCheckBox("Furos de Perfuração", ctrlFrame);
    cbDrills->setChecked(true);
    ctrlLayout->addWidget(cbDrills);
    
    // Divider
    auto* div1 = new QFrame(ctrlFrame);
    div1->setFrameShape(QFrame::HLine);
    div1->setFrameShadow(QFrame::Sunken);
    div1->setStyleSheet("color: #E2E8F0;");
    ctrlLayout->addWidget(div1);
    
    // Section: Tools
    auto* toolsTitle = new QLabel("Ferramentas", ctrlFrame);
    toolsTitle->setObjectName("sectionTitle");
    ctrlLayout->addWidget(toolsTitle);
    
    auto* btnMeasure = new QPushButton("Régua de Medição", ctrlFrame);
    btnMeasure->setObjectName("toolBtn");
    btnMeasure->setCheckable(true);
    ctrlLayout->addWidget(btnMeasure);
    
    // Divider
    auto* div2 = new QFrame(ctrlFrame);
    div2->setFrameShape(QFrame::HLine);
    div2->setFrameShadow(QFrame::Sunken);
    div2->setStyleSheet("color: #E2E8F0;");
    ctrlLayout->addWidget(div2);
    
    // Section: Quick Export
    auto* exportTitle = new QLabel("Exportar Camada", ctrlFrame);
    exportTitle->setObjectName("sectionTitle");
    ctrlLayout->addWidget(exportTitle);
    
    auto* btnExportTracks = new QPushButton("Exportar Apenas Trilhas", ctrlFrame);
    btnExportTracks->setObjectName("toolBtn");
    ctrlLayout->addWidget(btnExportTracks);
    
    auto* btnExportDrills = new QPushButton("Exportar Apenas Furos", ctrlFrame);
    btnExportDrills->setObjectName("toolBtn");
    ctrlLayout->addWidget(btnExportDrills);
    
    ctrlLayout->addStretch();
    mainHLayout->addWidget(ctrlFrame);

    // Scrollable image area (Right side)
    auto* scroll = new QScrollArea(&previewDlg);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setWidgetResizable(false);

    auto* imgLabel = new PcbPreviewLabel(scroll);
    imgLabel->setAlignment(Qt::AlignCenter);

    QPixmap pxFull = QPixmap::fromImage(previewImage);
    
    // SCALE CALCULATION FOR 1:1 REAL WORLD SCALE
    double imagePixelsPerMm = 3.937 * 5.0;
    QScreen *screen = QGuiApplication::primaryScreen();
    double screenDpi = screen ? screen->logicalDotsPerInch() : 96.0;
    double screenPixelsPerMm = screenDpi / 25.4;
    double realScale = screenPixelsPerMm / imagePixelsPerMm;
    
    QSize displaySize = pxFull.size() * realScale;
    QPixmap pxReal = pxFull.scaled(displaySize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    
    imgLabel->setPixmap(pxReal);
    imgLabel->adjustSize();

    scroll->setWidget(imgLabel);
    mainHLayout->addWidget(scroll, 1);
    
    pvLayout->addLayout(mainHLayout, 1);

    // Info hint
    auto* pvMeta = new QLabel(QString("Escala: REAL (1:1)  |  Resolução: %1 × %2 px")
                                  .arg(previewImage.width()).arg(previewImage.height()), &previewDlg);
    pvMeta->setObjectName("meta");
    pvMeta->setContentsMargins(0, 8, 0, 0);
    pvLayout->addWidget(pvMeta);

    // Action buttons row
    auto* pvBtnRow = new QHBoxLayout();
    pvBtnRow->setSpacing(10);
    pvBtnRow->setContentsMargins(0, 14, 0, 0);
    pvBtnRow->addStretch();

    auto* btnDiscard = new QPushButton("Fechar", &previewDlg);
    btnDiscard->setObjectName("discard");
    auto* btnSave    = new QPushButton("Salvar Visualização Atual", &previewDlg);
    
    pvBtnRow->addWidget(btnDiscard);
    pvBtnRow->addWidget(btnSave);
    pvLayout->addLayout(pvBtnRow);

    // Setup measurement factors for 1:1 scale
    imgLabel->setScaleFactors(1.0, 1.0 / screenPixelsPerMm);

    QDialog* dlgPtr = &previewDlg;

    // Lambda to update preview image dynamically based on selected layers
    auto updatePreview = [this, cbTracks, cbDrills, trackWidthMil, lineWidthMil, &previewImage, &pxFull, realScale, imgLabel, pvMeta]() {
        bool showTracks = cbTracks->isChecked();
        bool showDrills = cbDrills->isChecked();

        QImage newImg;
        if (showTracks && showDrills) {
            newImg = PcbExporter::generateLaserImage(
                m_scene->components(), m_scene->cables(), trackWidthMil, lineWidthMil, true);
        } else if (showTracks) {
            newImg = PcbExporter::generateLaserImage(
                m_scene->components(), m_scene->cables(), trackWidthMil, lineWidthMil, false);
        } else if (showDrills) {
            newImg = PcbExporter::generateDrillImage(m_scene->components());
        } else {
            // Both unchecked -> empty white image
            newImg = QImage(previewImage.size(), QImage::Format_RGB32);
            newImg.fill(Qt::white);
        }

        previewImage = newImg;
        pxFull = QPixmap::fromImage(previewImage);
        
        QSize displaySize = pxFull.size() * realScale;
        QPixmap pxReal = pxFull.scaled(displaySize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        
        imgLabel->setPixmap(pxReal);
        imgLabel->adjustSize();
        
        pvMeta->setText(QString("Escala: REAL (1:1)  |  Resolução: %1 × %2 px")
                            .arg(previewImage.width()).arg(previewImage.height()));
    };

    connect(cbTracks, &QCheckBox::toggled, this, [updatePreview]() { updatePreview(); });
    connect(cbDrills, &QCheckBox::toggled, this, [updatePreview]() { updatePreview(); });

    connect(btnMeasure, &QPushButton::toggled, this, [imgLabel, btnMeasure](bool checked){
        imgLabel->setMeasuring(checked);
        btnMeasure->setText(checked ? "Parar Medição" : "Régua de Medição");
    });

    // Sidebar export actions
    connect(btnExportTracks, &QPushButton::clicked, this, [this, dlgPtr, trackWidthMil, lineWidthMil]() {
        QString filePath = QFileDialog::getSaveFileName(dlgPtr,
            "Salvar Apenas Trilhas", "", "PNG Image (*.png)");
        if (filePath.isEmpty()) return;
        QImage img = PcbExporter::generateLaserImage(
            m_scene->components(), m_scene->cables(), trackWidthMil, lineWidthMil, false);
        if (img.save(filePath, "PNG")) {
            logMessage(QString("Trilhas exportadas: %1").arg(filePath), "SUCCESS");
            QMessageBox::information(dlgPtr, "Sucesso", "Imagem das trilhas exportada com sucesso!");
        } else {
            logMessage("Falha ao salvar imagem de trilhas.", "ERROR");
            QMessageBox::critical(dlgPtr, "Erro", "Não foi possível salvar o arquivo.");
        }
    });

    connect(btnExportDrills, &QPushButton::clicked, this, [this, dlgPtr]() {
        QString filePath = QFileDialog::getSaveFileName(dlgPtr,
            "Salvar Apenas Furos", "", "PNG Image (*.png)");
        if (filePath.isEmpty()) return;
        QImage img = PcbExporter::generateDrillImage(m_scene->components());
        if (img.save(filePath, "PNG")) {
            logMessage(QString("Furos exportados: %1").arg(filePath), "SUCCESS");
            QMessageBox::information(dlgPtr, "Sucesso", "Imagem dos furos exportada com sucesso!");
        } else {
            logMessage("Falha ao salvar imagem de furos.", "ERROR");
            QMessageBox::critical(dlgPtr, "Erro", "Não foi possível salvar o arquivo.");
        }
    });

    connect(btnDiscard, &QPushButton::clicked, &previewDlg, &QDialog::reject);
    connect(btnSave,    &QPushButton::clicked, &previewDlg, &QDialog::accept);

    if (previewDlg.exec() != QDialog::Accepted) {
        logMessage("Export laser cancelado pelo usuário.", "INFO");
        return;
    }

    // ── Step 4: Save ────────────────────────────────────────────────────────
    QString filePath = QFileDialog::getSaveFileName(this,
        "Salvar Layout Laser", "", "PNG Image (*.png)");
    if (filePath.isEmpty()) return;

    bool ok = previewImage.save(filePath, "PNG");
    if (ok) {
        logMessage(QString("Layout PCB exportado: %1").arg(filePath), "SUCCESS");
        QMessageBox::information(this, "Exportado", "Layout salvo com sucesso!");
        statusBar()->showMessage("Layout laser exportado!", 5000);
    } else {
        logMessage("Falha ao salvar o PNG do layout laser.", "ERROR");
        QMessageBox::critical(this, "Erro", "Falha ao salvar a imagem.");
    }
}


void MainWindow::editCapacitorProperties(CapacitorItem* capacitor) {
    if (!capacitor) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Configurar Capacitor");
    dialog.setMinimumWidth(340);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QDoubleSpinBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QDoubleSpinBox:focus { border-color: #93C5FD; }"
        "QComboBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QComboBox:focus { border-color: #93C5FD; }"
        "QComboBox QAbstractItemView { background-color: #FFFFFF; color: #0F172A; selection-background-color: #DBEAFE; selection-color: #0F172A; border: 1px solid #E6EEF3; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(14);
    layout->setContentsMargins(20, 20, 20, 20);

    // Capacitance inputs
    auto* capLabel = new QLabel("Capacitância (µF):", &dialog);
    layout->addWidget(capLabel);

    auto* spinBox = new QDoubleSpinBox(&dialog);
    spinBox->setRange(0.001, 100000.0);
    spinBox->setDecimals(3);
    spinBox->setSingleStep(1.0);
    spinBox->setValue(capacitor->capacitance());
    layout->addWidget(spinBox);

    // Quick shortcuts
    auto* quickLabel = new QLabel("Atalhos Rápidos:", &dialog);
    layout->addWidget(quickLabel);

    auto* quickBtnLayout = new QHBoxLayout();
    quickBtnLayout->setSpacing(6);

    auto* btn10n = new QPushButton("10 nF", &dialog);
    btn10n->setObjectName("cancelBtn");
    connect(btn10n, &QPushButton::clicked, [spinBox]() { spinBox->setValue(0.010); });

    auto* btn100n = new QPushButton("100 nF", &dialog);
    btn100n->setObjectName("cancelBtn");
    connect(btn100n, &QPushButton::clicked, [spinBox]() { spinBox->setValue(0.100); });

    auto* btn10u = new QPushButton("10 µF", &dialog);
    btn10u->setObjectName("cancelBtn");
    connect(btn10u, &QPushButton::clicked, [spinBox]() { spinBox->setValue(10.0); });

    auto* btn100u = new QPushButton("100 µF", &dialog);
    btn100u->setObjectName("cancelBtn");
    connect(btn100u, &QPushButton::clicked, [spinBox]() { spinBox->setValue(100.0); });

    quickBtnLayout->addWidget(btn10n);
    quickBtnLayout->addWidget(btn100n);
    quickBtnLayout->addWidget(btn10u);
    quickBtnLayout->addWidget(btn100u);
    layout->addLayout(quickBtnLayout);

    // Packaging Type
    auto* pkgLabel = new QLabel("Encapsulamento:", &dialog);
    layout->addWidget(pkgLabel);

    auto* pkgCombo = new QComboBox(&dialog);
    pkgCombo->addItem("Clássico (Disco Cerâmico Laranja)", false);
    pkgCombo->addItem("SMD (Montagem em Superfície Retangular)", true);
    
    // Set current index based on component status
    pkgCombo->setCurrentIndex(capacitor->isSMD() ? 1 : 0);
    layout->addWidget(pkgCombo);

    // SMD size selector (only enabled when SMD is selected)
    auto* sizeLabel = new QLabel("Tamanho SMD:", &dialog);
    layout->addWidget(sizeLabel);

    auto* sizeCombo = new QComboBox(&dialog);
    sizeCombo->addItems({"0805", "1206", "1210", "1812", "2220"});
    
    int currentSizeIdx = sizeCombo->findText(capacitor->smdSize());
    if (currentSizeIdx != -1) {
        sizeCombo->setCurrentIndex(currentSizeIdx);
    } else {
        sizeCombo->setCurrentIndex(1); // Default to 1206
    }
    
    layout->addWidget(sizeCombo);

    // Manage enabling/disabling SMD size combo based on encapsulation choice
    auto updateSmdVisibility = [sizeLabel, sizeCombo](bool isSMD) {
        sizeLabel->setEnabled(isSMD);
        sizeCombo->setEnabled(isSMD);
    };

    connect(pkgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [pkgCombo, updateSmdVisibility](int index) {
        bool isSMD = pkgCombo->itemData(index).toBool();
        updateSmdVisibility(isSMD);
    });

    // Initialize state
    updateSmdVisibility(capacitor->isSMD());

    // Automatically check SMD if capacitance changes to >= 10 uF as premium assist,
    // but allow the user to manually change it later
    connect(spinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [pkgCombo](double val) {
        if (val >= 10.0) {
            pkgCombo->setCurrentIndex(1); // Auto SMD
        } else {
            pkgCombo->setCurrentIndex(0); // Auto Classic
        }
    });

    // Accept / Cancel actions
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Salvar", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        double newCapVal = spinBox->value();
        bool newIsSMD = pkgCombo->currentData().toBool();
        QString newSmdSize = sizeCombo->currentText();

        capacitor->setCapacitance(newCapVal);
        capacitor->setSMD(newIsSMD);
        if (newIsSMD) {
            capacitor->setSmdSize(newSmdSize);
        }

        compileCode();
        statusBar()->showMessage(QString("Capacitor %1 configurado para %2 µF (%3)")
            .arg(capacitor->id())
            .arg(newCapVal)
            .arg(newIsSMD ? "SMD " + newSmdSize : "Disco Cerâmico"), 3000);
    }
}


void MainWindow::editLEDProperties(LEDItem* led) {
    if (!led) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Configurar LED");
    dialog.setMinimumWidth(340);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QComboBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QComboBox:focus { border-color: #93C5FD; }"
        "QComboBox QAbstractItemView { background-color: #FFFFFF; color: #0F172A; selection-background-color: #DBEAFE; selection-color: #0F172A; border: 1px solid #E6EEF3; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(14);
    layout->setContentsMargins(20, 20, 20, 20);

    // Packaging Type
    auto* pkgLabel = new QLabel("Encapsulamento:", &dialog);
    layout->addWidget(pkgLabel);

    auto* pkgCombo = new QComboBox(&dialog);
    pkgCombo->addItem("Clássico (Dome Cilíndrico de 5mm)", false);
    pkgCombo->addItem("SMD (Montagem em Superfície)", true);
    
    bool isLedSMD = led->property("isSMD").toBool();
    pkgCombo->setCurrentIndex(isLedSMD ? 1 : 0);
    layout->addWidget(pkgCombo);

    // SMD size selector
    auto* sizeLabel = new QLabel("Tamanho SMD:", &dialog);
    layout->addWidget(sizeLabel);

    auto* sizeCombo = new QComboBox(&dialog);
    sizeCombo->addItems({"0603", "0805", "1206", "5050"});
    
    QString currentSize = led->property("smdSize").toString();
    int currentSizeIdx = sizeCombo->findText(currentSize);
    if (currentSizeIdx != -1) {
        sizeCombo->setCurrentIndex(currentSizeIdx);
    } else {
        sizeCombo->setCurrentIndex(1); // Default to 0805
    }
    layout->addWidget(sizeCombo);

    auto updateSmdVisibility = [sizeLabel, sizeCombo](bool isSMD) {
        sizeLabel->setEnabled(isSMD);
        sizeCombo->setEnabled(isSMD);
    };

    connect(pkgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [pkgCombo, updateSmdVisibility](int index) {
        bool isSMD = pkgCombo->itemData(index).toBool();
        updateSmdVisibility(isSMD);
    });

    updateSmdVisibility(isLedSMD);

    // Accept / Cancel action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Salvar", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        bool newIsSMD = pkgCombo->currentData().toBool();
        QString newSmdSize = sizeCombo->currentText();

        led->setProperty("isSMD", newIsSMD);
        if (newIsSMD) {
            led->setProperty("smdSize", newSmdSize);
            led->updateLayoutForSMD(newSmdSize);
        } else {
            // Restore original pins localPos for non-SMD LED
            led->pins()[0].localPos = QPointF(-10, 30);
            led->pins()[1].localPos = QPointF(10, 30);
            led->update();
        }

        compileCode();
        statusBar()->showMessage(QString("LED %1 configurado para %2")
            .arg(led->id())
            .arg(newIsSMD ? "SMD " + newSmdSize : "Dome Cilíndrico"), 3000);
    }
}


void MainWindow::editResistorValue(ResistorItem* resistor) {
    if (!resistor) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Editar Resistência");
    dialog.setMinimumWidth(340);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QDoubleSpinBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QDoubleSpinBox:focus { border-color: #93C5FD; }"
        "QComboBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QComboBox:focus { border-color: #93C5FD; }"
        "QComboBox QAbstractItemView { background-color: #FFFFFF; color: #0F172A; selection-background-color: #DBEAFE; selection-color: #0F172A; border: 1px solid #E6EEF3; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(14);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* titleLabel = new QLabel("Valor da Resistência (Ω):", &dialog);
    layout->addWidget(titleLabel);

    auto* spinBox = new QDoubleSpinBox(&dialog);
    spinBox->setRange(0.1, 10000000.0); // 0.1 ohm to 10 mega ohms
    spinBox->setDecimals(1);
    spinBox->setSingleStep(10.0);
    spinBox->setValue(resistor->resistance());
    layout->addWidget(spinBox);

    // Add some common pre-configured quick buttons: 220Ω, 1kΩ, 10kΩ
    auto* quickLabel = new QLabel("Atalhos Rápidos:", &dialog);
    layout->addWidget(quickLabel);
    
    auto* quickBtnLayout = new QHBoxLayout();
    quickBtnLayout->setSpacing(8);
    
    auto* btn220 = new QPushButton("220 Ω", &dialog);
    btn220->setObjectName("cancelBtn"); // Secondary style
    connect(btn220, &QPushButton::clicked, [spinBox]() { spinBox->setValue(220.0); });
    
    auto* btn1k = new QPushButton("1 kΩ", &dialog);
    btn1k->setObjectName("cancelBtn");
    connect(btn1k, &QPushButton::clicked, [spinBox]() { spinBox->setValue(1000.0); });
    
    auto* btn10k = new QPushButton("10 kΩ", &dialog);
    btn10k->setObjectName("cancelBtn");
    connect(btn10k, &QPushButton::clicked, [spinBox]() { spinBox->setValue(10000.0); });

    quickBtnLayout->addWidget(btn220);
    quickBtnLayout->addWidget(btn1k);
    quickBtnLayout->addWidget(btn10k);
    layout->addLayout(quickBtnLayout);

    // Packaging Type
    auto* pkgLabel = new QLabel("Encapsulamento:", &dialog);
    layout->addWidget(pkgLabel);

    auto* pkgCombo = new QComboBox(&dialog);
    pkgCombo->addItem("Clássico (Trough-hole / Com Terminais)", false);
    pkgCombo->addItem("SMD (Montagem em Superfície)", true);
    
    bool isResistorSMD = resistor->property("isSMD").toBool();
    pkgCombo->setCurrentIndex(isResistorSMD ? 1 : 0);
    layout->addWidget(pkgCombo);

    // SMD size selector
    auto* sizeLabel = new QLabel("Tamanho SMD:", &dialog);
    layout->addWidget(sizeLabel);

    auto* sizeCombo = new QComboBox(&dialog);
    sizeCombo->addItems({"0402", "0603", "0805", "1206"});
    
    QString currentSize = resistor->property("smdSize").toString();
    int currentSizeIdx = sizeCombo->findText(currentSize);
    if (currentSizeIdx != -1) {
        sizeCombo->setCurrentIndex(currentSizeIdx);
    } else {
        sizeCombo->setCurrentIndex(3); // Default to 1206
    }
    layout->addWidget(sizeCombo);

    auto updateSmdVisibility = [sizeLabel, sizeCombo](bool isSMD) {
        sizeLabel->setEnabled(isSMD);
        sizeCombo->setEnabled(isSMD);
    };

    connect(pkgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [pkgCombo, updateSmdVisibility](int index) {
        bool isSMD = pkgCombo->itemData(index).toBool();
        updateSmdVisibility(isSMD);
    });

    updateSmdVisibility(isResistorSMD);

    // Accept / Cancel action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Salvar", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        double newValue = spinBox->value();
        resistor->setResistance(newValue);

        if (m_tutorialOverlay && m_tutorialOverlay->isVisible()) {
            int step = m_tutorialOverlay->currentStep();
            if (m_activeTutorial == 1 && step == 3 && qAbs(newValue - 220.0) < 0.1) {
                m_tutorialOverlay->advance();
            } else if (m_activeTutorial == 4 && step == 3 && qAbs(newValue - 10000.0) < 0.1) {
                m_tutorialOverlay->advance();
            }
        }
        
        bool newIsSMD = pkgCombo->currentData().toBool();
        QString newSmdSize = sizeCombo->currentText();
        resistor->setProperty("isSMD", newIsSMD);
        if (newIsSMD) {
            resistor->setProperty("smdSize", newSmdSize);
            resistor->updateLayoutForSMD(newSmdSize);
        } else {
            resistor->pins()[0].localPos = QPointF(-30, 0);
            resistor->pins()[1].localPos = QPointF(30, 0);
            resistor->update();
        }
        
        // Force workspace compile update in real-time
        compileCode();
        statusBar()->showMessage(QString("Resistência de %1 atualizada para %2 Ω (%3)").arg(resistor->id()).arg(newValue).arg(newIsSMD ? "SMD " + newSmdSize : "Clássico"), 3000);
    }
}

void MainWindow::editPotentiometerValue(PotentiometerItem* potentiometer) {
    if (!potentiometer) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Ajustar Rotação");
    dialog.setMinimumWidth(320);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QSpinBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QSpinBox:focus { border-color: #93C5FD; }"
        "QSlider::groove:horizontal { border: 1px solid #E6EEF3; height: 6px; background: #F8FAFC; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #2563EB; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #1E40AF; border: 1px solid #93C5FD; width: 14px; margin-top: -4px; margin-bottom: -4px; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #1D4ED8; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(16);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* titleLabel = new QLabel("Posição da Rotação (0 - 100%):", &dialog);
    layout->addWidget(titleLabel);

    auto* sliderLay = new QHBoxLayout();
    sliderLay->setSpacing(10);

    auto* slider = new QSlider(Qt::Horizontal, &dialog);
    slider->setRange(0, 100);
    slider->setValue(static_cast<int>(potentiometer->value()));

    auto* spinBox = new QSpinBox(&dialog);
    spinBox->setRange(0, 100);
    spinBox->setSuffix(" %");
    spinBox->setValue(static_cast<int>(potentiometer->value()));
    spinBox->setFixedWidth(80);

    sliderLay->addWidget(slider);
    sliderLay->addWidget(spinBox);
    layout->addLayout(sliderLay);

    // Sync Slider and SpinBox
    connect(slider, &QSlider::valueChanged, spinBox, &QSpinBox::setValue);
    connect(spinBox, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);

    // Quick presets: 0%, 50%, 100%
    auto* quickLabel = new QLabel("Predefinições:", &dialog);
    layout->addWidget(quickLabel);

    auto* quickBtnLayout = new QHBoxLayout();
    quickBtnLayout->setSpacing(6);

    auto* btn0 = new QPushButton("0%", &dialog);
    btn0->setObjectName("cancelBtn");
    connect(btn0, &QPushButton::clicked, [slider]() { slider->setValue(0); });

    auto* btn50 = new QPushButton("50%", &dialog);
    btn50->setObjectName("cancelBtn");
    connect(btn50, &QPushButton::clicked, [slider]() { slider->setValue(50); });

    auto* btn100 = new QPushButton("100%", &dialog);
    btn100->setObjectName("cancelBtn");
    connect(btn100, &QPushButton::clicked, [slider]() { slider->setValue(100); });

    quickBtnLayout->addWidget(btn0);
    quickBtnLayout->addWidget(btn50);
    quickBtnLayout->addWidget(btn100);
    layout->addLayout(quickBtnLayout);

    // Action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Salvar", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        int newValue = slider->value();
        potentiometer->setValue(newValue);
        // Force compile update in real-time
        compileCode();
        statusBar()->showMessage(QString("Potenciômetro %1 atualizado para %2%").arg(potentiometer->id()).arg(newValue), 3000);
    }
}

void MainWindow::editLdrValue(LdrItem* ldr) {
    if (!ldr) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Ajustar Luminosidade");
    dialog.setMinimumWidth(320);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QSpinBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QSpinBox:focus { border-color: #93C5FD; }"
        "QSlider::groove:horizontal { border: 1px solid #E6EEF3; height: 6px; background: #F8FAFC; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #10B981; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #047857; border: 1px solid #A7F3D0; width: 14px; margin-top: -4px; margin-bottom: -4px; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #065F46; }"
        "QPushButton { background: #10B981; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #065F46; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(16);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* titleLabel = new QLabel("Nível de Luz do LDR (0 - 100%):", &dialog);
    layout->addWidget(titleLabel);

    auto* sliderLay = new QHBoxLayout();
    sliderLay->setSpacing(10);

    auto* slider = new QSlider(Qt::Horizontal, &dialog);
    slider->setRange(0, 100);
    slider->setValue(static_cast<int>(ldr->value()));

    auto* spinBox = new QSpinBox(&dialog);
    spinBox->setRange(0, 100);
    spinBox->setSuffix(" %");
    spinBox->setValue(static_cast<int>(ldr->value()));
    spinBox->setFixedWidth(80);

    sliderLay->addWidget(slider);
    sliderLay->addWidget(spinBox);
    layout->addLayout(sliderLay);

    // Sync Slider and SpinBox
    connect(slider, &QSlider::valueChanged, spinBox, &QSpinBox::setValue);
    connect(spinBox, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);

    // Quick presets: 10% (Escuro), 50% (Ambiente), 90% (Claro)
    auto* quickLabel = new QLabel("Predefinições:", &dialog);
    layout->addWidget(quickLabel);

    auto* quickBtnLayout = new QHBoxLayout();
    quickBtnLayout->setSpacing(6);

    auto* btnEscuro = new QPushButton("Escuro (10%)", &dialog);
    btnEscuro->setObjectName("cancelBtn");
    connect(btnEscuro, &QPushButton::clicked, [slider]() { slider->setValue(10); });

    auto* btnAmbiente = new QPushButton("Ambiente (50%)", &dialog);
    btnAmbiente->setObjectName("cancelBtn");
    connect(btnAmbiente, &QPushButton::clicked, [slider]() { slider->setValue(50); });

    auto* btnClaro = new QPushButton("Claro (90%)", &dialog);
    btnClaro->setObjectName("cancelBtn");
    connect(btnClaro, &QPushButton::clicked, [slider]() { slider->setValue(90); });

    quickBtnLayout->addWidget(btnEscuro);
    quickBtnLayout->addWidget(btnAmbiente);
    quickBtnLayout->addWidget(btnClaro);
    layout->addLayout(quickBtnLayout);

    // Action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Salvar", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        int newValue = slider->value();
        ldr->setValue(newValue);
        // Force compile update in real-time
        compileCode();
        statusBar()->showMessage(QString("Luminosidade do LDR %1 atualizada para %2%").arg(ldr->id()).arg(newValue), 3000);
    }
}

void MainWindow::editDHT22Properties(DHT22Item* dht) {
    if (!dht) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Ajustar DHT22");
    dialog.setMinimumWidth(340);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QSpinBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QSpinBox:focus { border-color: #93C5FD; }"
        "QSlider::groove:horizontal { border: 1px solid #E6EEF3; height: 6px; background: #F8FAFC; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #2563EB; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #1E40AF; border: 1px solid #93C5FD; width: 14px; margin-top: -4px; margin-bottom: -4px; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #1D4ED8; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(14);
    layout->setContentsMargins(20, 20, 20, 20);

    // Humidity section
    auto* humLabel = new QLabel("Umidade (0 - 100%):", &dialog);
    layout->addWidget(humLabel);

    auto* humLay = new QHBoxLayout();
    humLay->setSpacing(10);

    auto* humSlider = new QSlider(Qt::Horizontal, &dialog);
    humSlider->setRange(0, 100);
    humSlider->setValue(static_cast<int>(dht->humidity()));

    auto* humSpinBox = new QSpinBox(&dialog);
    humSpinBox->setRange(0, 100);
    humSpinBox->setSuffix(" %");
    humSpinBox->setValue(static_cast<int>(dht->humidity()));
    humSpinBox->setFixedWidth(80);

    humLay->addWidget(humSlider);
    humLay->addWidget(humSpinBox);
    layout->addLayout(humLay);

    connect(humSlider, &QSlider::valueChanged, humSpinBox, &QSpinBox::setValue);
    connect(humSpinBox, qOverload<int>(&QSpinBox::valueChanged), humSlider, &QSlider::setValue);

    // Temperature section
    auto* tempLabel = new QLabel("Temperatura (-40 a 80 ºC):", &dialog);
    layout->addWidget(tempLabel);

    auto* tempLay = new QHBoxLayout();
    tempLay->setSpacing(10);

    auto* tempSlider = new QSlider(Qt::Horizontal, &dialog);
    tempSlider->setRange(-40, 80);
    tempSlider->setValue(static_cast<int>(dht->temperature()));

    auto* tempSpinBox = new QSpinBox(&dialog);
    tempSpinBox->setRange(-40, 80);
    tempSpinBox->setSuffix(" ºC");
    tempSpinBox->setValue(static_cast<int>(dht->temperature()));
    tempSpinBox->setFixedWidth(80);

    tempLay->addWidget(tempSlider);
    tempLay->addWidget(tempSpinBox);
    layout->addLayout(tempLay);

    connect(tempSlider, &QSlider::valueChanged, tempSpinBox, &QSpinBox::setValue);
    connect(tempSpinBox, qOverload<int>(&QSpinBox::valueChanged), tempSlider, &QSlider::setValue);

    // Action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Salvar", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        dht->setHumidity(humSlider->value());
        dht->setTemperature(tempSlider->value());
        compileCode();
        statusBar()->showMessage(QString("DHT22 %1 atualizado (U:%2%, T:%3ºC)").arg(dht->id()).arg(humSlider->value()).arg(tempSlider->value()), 3000);
    }
}

void MainWindow::editHCSR04Properties(HCSR04Item* hcsr) {
    if (!hcsr) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Ajustar HC-SR04");
    dialog.setMinimumWidth(340);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QSpinBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QSpinBox:focus { border-color: #93C5FD; }"
        "QSlider::groove:horizontal { border: 1px solid #E6EEF3; height: 6px; background: #F8FAFC; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #2563EB; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #1E40AF; border: 1px solid #93C5FD; width: 14px; margin-top: -4px; margin-bottom: -4px; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #1D4ED8; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(14);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* distLabel = new QLabel("Distância (2 - 400 cm):", &dialog);
    layout->addWidget(distLabel);

    auto* distLay = new QHBoxLayout();
    distLay->setSpacing(10);

    auto* distSlider = new QSlider(Qt::Horizontal, &dialog);
    distSlider->setRange(2, 400);
    distSlider->setValue(static_cast<int>(hcsr->distance()));

    auto* distSpinBox = new QSpinBox(&dialog);
    distSpinBox->setRange(2, 400);
    distSpinBox->setSuffix(" cm");
    distSpinBox->setValue(static_cast<int>(hcsr->distance()));
    distSpinBox->setFixedWidth(80);

    distLay->addWidget(distSlider);
    distLay->addWidget(distSpinBox);
    layout->addLayout(distLay);

    connect(distSlider, &QSlider::valueChanged, distSpinBox, &QSpinBox::setValue);
    connect(distSpinBox, qOverload<int>(&QSpinBox::valueChanged), distSlider, &QSlider::setValue);

    // Action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Salvar", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        hcsr->setDistance(distSlider->value());
        compileCode();
        statusBar()->showMessage(QString("HC-SR04 %1 atualizado para %2 cm").arg(hcsr->id()).arg(distSlider->value()), 3000);
    }
}

void MainWindow::editMotorProperties(MotorItem* motor) {
    if (!motor) return;

    QDialog dialog(this);
    dialog.setWindowTitle("Configuração do Motor");
    dialog.setMinimumWidth(320);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QComboBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 6px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QComboBox QAbstractItemView { background: #FFFFFF; color: #0F172A; selection-background-color: #DBEAFE; selection-color: #1D4ED8; outline: none; }"
        "QDoubleSpinBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(14);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* titleLabel = new QLabel("Tipo de Motor:", &dialog);
    layout->addWidget(titleLabel);

    auto* typeCombo = new QComboBox(&dialog);
    typeCombo->addItem("Servo Motor (90°)", "servo90");
    typeCombo->addItem("Servo Motor (180°)", "servo180");
    typeCombo->addItem("Servo Motor (360° Contínuo)", "servo360");
    typeCombo->addItem("Motor DC Simples", "dc");
    typeCombo->addItem("Motor de Passo (Stepper)", "stepper");
    
    int idx = typeCombo->findData(motor->motorType());
    if (idx >= 0) typeCombo->setCurrentIndex(idx);
    
    layout->addWidget(typeCombo);

    auto* limitLabel = new QLabel("Limite (Graus / Velocidade):", &dialog);
    layout->addWidget(limitLabel);
    
    auto* limitSpin = new QDoubleSpinBox(&dialog);
    limitSpin->setRange(0, 5000);
    // If not set previously, default to 180 for standard servos
    double currentLimit = motor->property("motorLimit").toDouble();
    limitSpin->setValue(currentLimit == 0 ? 180.0 : currentLimit);
    layout->addWidget(limitSpin);

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Salvar", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        motor->setMotorType(typeCombo->currentData().toString());
        motor->setProperty("motorLimit", limitSpin->value());
        compileCode();
        statusBar()->showMessage(QString("Motor %1 configurado com sucesso!").arg(motor->id()), 3000);
    }
}


void MainWindow::onComponentAdded(ComponentItem* comp) {
    if (!comp) return;
    logMessage(QString("Componente '%1' (%2) inserido na cena física.").arg(comp->name()).arg(comp->id()), "SUCCESS");

    if (comp->componentType() == "esp32") {
        auto* esp = static_cast<ESP32Item*>(comp);
        connect(esp, &ESP32Item::resetTriggered, this, [this]() {
            if (m_simulator && m_simulator->isRunning()) {
                m_simulator->updateEventStorage(m_blockEditor->getEventBlockStorage());
                m_simulator->resetSimulation();
            }
        });
    }

    if (comp->componentType() == "potentiometer") {
        connect(static_cast<PotentiometerItem*>(comp), &PotentiometerItem::valueChanged, this, [this, comp](double) {
            compileCode();
            if (m_simulator && m_simulator->isRunning()) {
                m_simulator->triggerComponentEvent(comp->id(), "aoGirar");
            }
        });
    } else if (comp->componentType() == "ldr") {
        connect(static_cast<LdrItem*>(comp), &LdrItem::valueChanged, this, [this, comp](double) {
            compileCode();
            if (m_simulator && m_simulator->isRunning()) {
                m_simulator->triggerComponentEvent(comp->id(), "aoAlterar");
            }
        });
    } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
        if (custom->category() == "analog_input") {
            connect(custom, &CustomComponentItem::valueChanged, this, [this, custom](double) {
                compileCode();
                if (m_simulator && m_simulator->isRunning()) {
                    m_simulator->triggerComponentEvent(custom->id(), "aoGirar");
                }
            });
        } else if (custom->category() == "digital_trigger") {
            connect(custom, &CustomComponentItem::stateChanged, this, [this](bool) {
                compileCode();
            });
        }
    }
}

void MainWindow::readNativeSimOutput() {
    if (!m_nativeSimProcess) return;
    QByteArray data = m_nativeSimProcess->readAllStandardOutput();
    QString text = QString::fromUtf8(data);
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    
    QVariantMap pinMap = m_scene->property("nativePinMap").toMap();
    
    for (const QString& line : lines) {
        if (line.startsWith("PIN:")) {
            QStringList parts = line.split(':');
            if (parts.size() >= 3) {
                int pin = parts[1].toInt();
                bool high = (parts[2].trimmed() == "HIGH");
                
                QString compId = pinMap.value(QString::number(pin)).toString();
                if (!compId.isEmpty()) {
                    ComponentItem* comp = nullptr;
                    for (auto* c : m_scene->components()) {
                        if (c->id() == compId) { comp = c; break; }
                    }
                    if (comp) {
                        if (comp->componentType() == "led" || comp->componentType() == "rgb_led") {
                            // Needs include LedItem.h but actually we can set property
                            comp->setProperty("turnedOn", high);
                            comp->update();
                        } else if (comp->componentType() == "buzzer") {
                            comp->setProperty("active", high);
                            comp->update();
                        }
                    }
                }
                // Inform oscilloscope
                m_oscilloscope->onPinStateChanged(compId, QString::number(pin), high);
            }
        } else if (line.startsWith("PWM:")) {
            QStringList parts = line.split(':');
            if (parts.size() >= 3) {
                int pin = parts[1].toInt();
                int val = parts[2].toInt();
                QString compId = pinMap.value(QString::number(pin)).toString();
                if (!compId.isEmpty()) {
                    m_oscilloscope->onPinStateChanged(compId, QString::number(pin), val > 127);
                }
            }
        } else if (line.startsWith("TONE:")) {
            QStringList parts = line.split(':');
            if (parts.size() >= 3) {
                int pin = parts[1].toInt();
                int freq = parts[2].toInt();
                QString compId = pinMap.value(QString::number(pin)).toString();
                // update buzzer frequency visually/sound? (skipping sound for now)
            }
        } else if (line.startsWith("SERIAL:")) {
            m_serialMonitor->appendPlainText(line.mid(7).trimmed());
            m_serialMonitor->verticalScrollBar()->setValue(m_serialMonitor->verticalScrollBar()->maximum());
        }
    }
}

void MainWindow::openComponentCreator() {
    logMessage("Assistente de modelagem de componentes iniciado.", "SYSTEM");
    ComponentCreatorDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        CustomComponentDef def = dialog.getCreatedComponent();
        CustomComponentManager::instance().registerComponent(def);
        loadToolboxItems();
        logMessage(QString("Novo componente personalizado '%1' modelado e registrado com sucesso!").arg(def.name), "SUCCESS");
        statusBar()->showMessage(QString("Componente '%1' modelado e registrado!").arg(def.name), 4000);
    } else {
        logMessage("Modelagem de componentes cancelada pelo usuário.", "INFO");
    }
}

void MainWindow::loadToolboxItems() {
    m_toolboxList->clear();

    // 1. Add native built-in components
    auto* ledItem = new QListWidgetItem("LED", m_toolboxList);
    ledItem->setData(Qt::UserRole, "led");
    auto* rgbLedItem = new QListWidgetItem("LED RGB", m_toolboxList);
    rgbLedItem->setData(Qt::UserRole, "rgb_led");
    auto* btnItem = new QListWidgetItem("Botão", m_toolboxList);
    btnItem->setData(Qt::UserRole, "button");
    auto* resItem = new QListWidgetItem("Resistor", m_toolboxList);
    resItem->setData(Qt::UserRole, "resistor");
    auto* capItem = new QListWidgetItem("Capacitor", m_toolboxList);
    capItem->setData(Qt::UserRole, "capacitor");
    auto* gndItem = new QListWidgetItem("Terra (GND)", m_toolboxList);
    gndItem->setData(Qt::UserRole, "gnd");
    auto* potItem = new QListWidgetItem("Potenciômetro", m_toolboxList);
    potItem->setData(Qt::UserRole, "potentiometer");
    auto* ldrItem = new QListWidgetItem("Sensor LDR (Luz)", m_toolboxList);
    ldrItem->setData(Qt::UserRole, "ldr");
    auto* buzItem = new QListWidgetItem("Buzzer 5V", m_toolboxList);
    buzItem->setData(Qt::UserRole, "buzzer");
    auto* relayItem = new QListWidgetItem("Módulo Relé", m_toolboxList);
    relayItem->setData(Qt::UserRole, "relay");
    auto* dhtItem = new QListWidgetItem("Sensor Temperatura/Umidade DHT22", m_toolboxList);
    dhtItem->setData(Qt::UserRole, "dht22");
    auto* hcsrItem = new QListWidgetItem("Sensor Ultrassônico HC-SR04", m_toolboxList);
    hcsrItem->setData(Qt::UserRole, "hcsr04");

    // 2. Add custom modeled components
    for (const auto& def : CustomComponentManager::instance().registeredComponents()) {
        auto* customItem = new QListWidgetItem(def.name, m_toolboxList);
        customItem->setData(Qt::UserRole, def.type);
    }
}

void MainWindow::onToolboxContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_toolboxList->itemAt(pos);
    if (!item) return;

    QString typeId = item->data(Qt::UserRole).toString();

    // Check if it's a native component
    QStringList builtIns = {"led", "button", "resistor", "capacitor", "potentiometer", "ldr", "buzzer", "esp32", "relay", "dht22", "hcsr04"};
    if (builtIns.contains(typeId)) return; // Don't allow deleting native components

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #FFFFFF; color: #0F172A; border: 1px solid #E6EEF3; }"
        "QMenu::item { padding: 6px 24px; }"
        "QMenu::item:selected { background-color: #EEF2FF; color: #1D4ED8; }"
    );
    QAction* delAct = menu.addAction("Deletar Componente Customizado");
    QAction* selected = menu.exec(m_toolboxList->mapToGlobal(pos));

    if (selected == delAct) {
        auto reply = QMessageBox::question(this, "Deletar Componente", 
            QString("Tem certeza que deseja deletar o componente '%1'?\n\nEle será removido permanentemente do registro.\nComponentes já criados na tela continuarão existindo até o fechamento da IDE.").arg(item->text()),
            QMessageBox::Yes | QMessageBox::No);
            
        if (reply == QMessageBox::Yes) {
            if (CustomComponentManager::instance().removeComponent(typeId)) {
                loadToolboxItems();
                statusBar()->showMessage(QString("Componente '%1' deletado com sucesso.").arg(item->text()), 3000);
            } else {
                QMessageBox::warning(this, "Erro", "Não foi possível deletar o componente.");
            }
        }
    }
}

void MainWindow::editCustomPotentiometerValue(CustomComponentItem* custom) {
    if (!custom) return;

    double minValue = custom->definition().minValue;
    double maxValue = custom->definition().maxValue;
    QString unit = custom->definition().valueUnit;
    if (unit.isEmpty()) unit = "%";

    QDialog dialog(this);
    dialog.setWindowTitle(QString("Injetar Valor - %1").arg(custom->name()));
    dialog.setMinimumWidth(360);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E6EEF3; }"
        "QLabel { color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; }"
        "QDoubleSpinBox { background: #FFFFFF; border: 1px solid #E6EEF3; border-radius: 6px; color: #0F172A; padding: 8px; font-size: 12px; font-weight: bold; selection-background-color: #DBEAFE; }"
        "QDoubleSpinBox:focus { border-color: #93C5FD; }"
        "QSlider::groove:horizontal { border: 1px solid #E6EEF3; height: 6px; background: #F8FAFC; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #2563EB; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #1E40AF; border: 1px solid #93C5FD; width: 14px; margin-top: -4px; margin-bottom: -4px; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #1D4ED8; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 18px; font-weight: bold; font-size: 12px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: rgba(15, 23, 42, 0.04); border: 1px solid #E6EEF3; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: rgba(15, 23, 42, 0.06); color: #0F172A; }"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(16);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* titleLabel = new QLabel(QString("Ajustar Leitura (%1 - %2 %3):").arg(minValue).arg(maxValue).arg(unit), &dialog);
    layout->addWidget(titleLabel);

    auto* sliderLay = new QHBoxLayout();
    sliderLay->setSpacing(12);

    auto* slider = new QSlider(Qt::Horizontal, &dialog);
    slider->setRange(0, 1000); // 1000 steps of precision
    
    double range = maxValue - minValue;
    if (range <= 0.0) range = 100.0;
    int initialSliderVal = static_cast<int>((custom->value() - minValue) / range * 1000.0);
    slider->setValue(qBound(0, initialSliderVal, 1000));

    auto* spinBox = new QDoubleSpinBox(&dialog);
    spinBox->setRange(minValue, maxValue);
    spinBox->setSuffix(" " + unit);
    spinBox->setDecimals(range <= 10.0 ? 2 : 1);
    spinBox->setValue(custom->value());
    spinBox->setFixedWidth(100);

    sliderLay->addWidget(slider);
    sliderLay->addWidget(spinBox);
    layout->addLayout(sliderLay);

    // Sync Slider (0-1000) and SpinBox (min-max)
    connect(slider, &QSlider::valueChanged, this, [spinBox, minValue, maxValue](int sv) {
        double val = minValue + (sv / 1000.0) * (maxValue - minValue);
        if (std::abs(spinBox->value() - val) > 0.001) {
            spinBox->setValue(val);
        }
    });
    connect(spinBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [slider, minValue, maxValue](double dv) {
        double range = maxValue - minValue;
        if (range > 0.0) {
            int sv = static_cast<int>((dv - minValue) / range * 1000.0);
            if (slider->value() != sv) {
                slider->setValue(sv);
            }
        }
    });

    // Quick presets: Min, Mid, Max
    auto* quickLabel = new QLabel("Predefinições:", &dialog);
    layout->addWidget(quickLabel);

    auto* quickBtnLayout = new QHBoxLayout();
    quickBtnLayout->setSpacing(6);

    auto* btnMin = new QPushButton("Mínimo", &dialog);
    btnMin->setObjectName("cancelBtn");
    connect(btnMin, &QPushButton::clicked, [spinBox, minValue]() { spinBox->setValue(minValue); });

    auto* btnMid = new QPushButton("Médio", &dialog);
    btnMid->setObjectName("cancelBtn");
    connect(btnMid, &QPushButton::clicked, [spinBox, minValue, maxValue]() { spinBox->setValue((minValue + maxValue) / 2.0); });

    auto* btnMax = new QPushButton("Máximo", &dialog);
    btnMax->setObjectName("cancelBtn");
    connect(btnMax, &QPushButton::clicked, [spinBox, maxValue]() { spinBox->setValue(maxValue); });

    quickBtnLayout->addWidget(btnMin);
    quickBtnLayout->addWidget(btnMid);
    quickBtnLayout->addWidget(btnMax);
    layout->addLayout(quickBtnLayout);

    // Action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* cancelBtn = new QPushButton("Cancelar", &dialog);
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    auto* saveBtn = new QPushButton("Injetar Valor", &dialog);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        double newValue = spinBox->value();
        custom->setValue(newValue);
        // Force compile update in real-time
        compileCode();
        statusBar()->showMessage(QString("Componente %1 atualizado para %2 %3").arg(custom->id()).arg(newValue).arg(unit), 3000);
    }
}

void MainWindow::editMicrocontroller(ComponentItem* comp) {
    if (!comp) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QString("Editar Microcontrolador - %1").arg(comp->name()));
    dialog.setMinimumWidth(520);
    dialog.setStyleSheet(
        "QDialog { background: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 12px; }"
        "QLabel { color: #334155; font-family: 'Segoe UI', Arial, sans-serif; font-size: 11px; font-weight: 600; }"
        "QLineEdit { background: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 6px; color: #0F172A; padding: 6px 10px; font-size: 12px; }"
        "QLineEdit:focus { border-color: #3B82F6; }"
        "QComboBox { background: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 6px; color: #0F172A; padding: 5px 10px; font-size: 12px; min-width: 80px; }"
        "QComboBox:focus { border-color: #3B82F6; }"
        "QDoubleSpinBox { background: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 6px; color: #0F172A; padding: 6px 10px; font-size: 12px; }"
        "QDoubleSpinBox:focus { border-color: #3B82F6; }"
        "QTableWidget { background: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 8px; color: #0F172A; font-size: 12px; gridline-color: #F1F5F9; }"
        "QTableWidget::item { padding: 6px; }"
        "QTableWidget::item:selected { background: #DBEAFE; color: #1E40AF; }"
        "QHeaderView::section { background: #F8FAFC; border: none; border-bottom: 1px solid #E2E8F0; color: #475569; font-weight: 600; font-size: 11px; padding: 6px; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: #FFFFFF; padding: 8px 16px; font-weight: 600; font-size: 12px; }"
        "QPushButton:hover { background: #1D4ED8; }"
        "QPushButton:pressed { background: #1E40AF; }"
        "QPushButton#cancelBtn { background: #F1F5F9; border: 1px solid #E2E8F0; color: #475569; }"
        "QPushButton#cancelBtn:hover { background: #E2E8F0; color: #0F172A; }"
        "QPushButton#secondaryBtn { background: #F1F5F9; border: 1px solid #E2E8F0; color: #475569; }"
        "QPushButton#secondaryBtn:hover { background: #E2E8F0; color: #0F172A; }"
    );

    auto* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // Top: board selection and framework
    auto* topLayout = new QHBoxLayout();
    auto* boardLabel = new QLabel("Placa:");
    topLayout->addWidget(boardLabel);

    auto* boardEdit = new QLineEdit(&dialog);
    boardEdit->setPlaceholderText("Ex: esp32dev, nodemcuv2...");
    topLayout->addWidget(boardEdit, 2);

    auto* searchBoardBtn = new QPushButton(&dialog);
    searchBoardBtn->setIcon(QIcon(":/icons/search.svg"));
    searchBoardBtn->setIconSize(QSize(18, 18));
    searchBoardBtn->setToolTip("Buscar placa no PlatformIO");
    searchBoardBtn->setFixedWidth(36);
    searchBoardBtn->setStyleSheet("QPushButton { background-color: #E2E8F0; border: 1px solid #CBD5E1; border-radius: 6px; padding: 5px; } QPushButton:hover { background-color: #CBD5E1; }");
    topLayout->addWidget(searchBoardBtn);

    auto* coreLabel = new QLabel("Framework:");
    topLayout->addWidget(coreLabel);
    auto* coreCombo = new QComboBox(&dialog);
    coreCombo->addItems({"arduino", "espidf"});
    topLayout->addWidget(coreCombo, 1);

    mainLayout->addLayout(topLayout);

    auto* searchFeedbackLabel = new QLabel("", &dialog);
    searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #64748B;");
    mainLayout->addWidget(searchFeedbackLabel);

    connect(searchBoardBtn, &QPushButton::clicked, this, [boardEdit, searchFeedbackLabel, this]() {
        QString query = boardEdit->text().trimmed();
        if (query.isEmpty()) {
            searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #EF4444;");
            searchFeedbackLabel->setText("Digite o nome de uma placa para pesquisar.");
            return;
        }
        
        searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #2563EB;");
        searchFeedbackLabel->setText("Buscando placa no PlatformIO...");
        QCoreApplication::processEvents();
        
        if (!ToolchainManager(this).platformIOIsInstalled()) {
            searchFeedbackLabel->setStyleSheet("font-size: 10px; color: #EF4444;");
            searchFeedbackLabel->setText("PlatformIO não instalado!");
            return;
        }
        
        QProcess p;
        QString pioCmd = ToolchainManager(this).getPlatformIOCommand();
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

    // Middle: pins table
    auto* pinsLabel = new QLabel("Pinos / Conexões:");
    mainLayout->addWidget(pinsLabel);

    // Board physical size and pin pitch
    auto* sizeLayout = new QHBoxLayout();
    auto* widthLabel = new QLabel("Largura (mm):");
    auto* widthSpin = new QDoubleSpinBox(&dialog);
    widthSpin->setRange(1, 500); widthSpin->setValue(54.0);
    auto* heightLabel = new QLabel("Altura (mm):");
    auto* heightSpin = new QDoubleSpinBox(&dialog);
    heightSpin->setRange(1, 500); heightSpin->setValue(25.0);
    auto* pitchLabel = new QLabel("Pin pitch (mm):");
    auto* pitchSpin = new QDoubleSpinBox(&dialog);
    pitchSpin->setRange(0.5, 10.0); pitchSpin->setDecimals(2); pitchSpin->setValue(2.54);
    sizeLayout->addWidget(widthLabel); sizeLayout->addWidget(widthSpin);
    sizeLayout->addWidget(heightLabel); sizeLayout->addWidget(heightSpin);
    sizeLayout->addWidget(pitchLabel); sizeLayout->addWidget(pitchSpin);
    mainLayout->addLayout(sizeLayout);

    auto* table = new QTableWidget(&dialog);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({"Nome", "Pino", "Papel"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(table, 1);

    // Buttons for pins
    auto* pinBtnLay = new QHBoxLayout();
    auto* addPinBtn = new QPushButton("Adicionar Pino"); addPinBtn->setObjectName("secondaryBtn");
    auto* removePinBtn = new QPushButton("Remover Pino"); removePinBtn->setObjectName("secondaryBtn");
    auto* jsonBtn = new QPushButton("JSON"); jsonBtn->setObjectName("secondaryBtn");
    pinBtnLay->addWidget(addPinBtn);
    pinBtnLay->addWidget(removePinBtn);
    pinBtnLay->addWidget(jsonBtn);
    pinBtnLay->addStretch();
    mainLayout->addLayout(pinBtnLay);

    // Load existing config if present
    QVariant existing = comp->property("microcontrollerConfig");
    if (existing.isValid() && existing.canConvert<QString>()) {
        QJsonDocument d = QJsonDocument::fromJson(existing.toString().toUtf8());
        if (d.isObject()) {
            QJsonObject o = d.object();
            if (o.contains("board")) boardEdit->setText(o["board"].toString());
            if (o.contains("core")) coreCombo->setCurrentText(o["core"].toString());
            if (o.contains("board_size") && o["board_size"].isObject()) {
                QJsonObject bs = o["board_size"].toObject();
                widthSpin->setValue(bs.value("width_mm").toDouble(widthSpin->value()));
                heightSpin->setValue(bs.value("height_mm").toDouble(heightSpin->value()));
            }
            if (o.contains("pin_pitch_mm")) pitchSpin->setValue(o.value("pin_pitch_mm").toDouble(pitchSpin->value()));
            if (o.contains("pins") && o["pins"].isArray()) {
                QJsonArray pa = o["pins"].toArray();
                for (const auto& pv : pa) {
                    if (!pv.isObject()) continue;
                    QJsonObject pj = pv.toObject();
                    int r = table->rowCount(); table->insertRow(r);
                    table->setItem(r, 0, new QTableWidgetItem(pj.value("name").toString()));
                    table->setItem(r, 1, new QTableWidgetItem(pj.value("pin").toString()));
                    table->setItem(r, 2, new QTableWidgetItem(pj.value("role").toString()));
                    if (pj.contains("side")) table->item(r,0)->setData(Qt::UserRole, pj.value("side").toString());
                }
            }
        }
    }

    // Pin buttons actions
    connect(addPinBtn, &QPushButton::clicked, this, [table]() {
        int r = table->rowCount(); table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem("PIN_NAME"));
        table->setItem(r, 1, new QTableWidgetItem("GPIO0"));
        table->setItem(r, 2, new QTableWidgetItem("signal"));
    });
    connect(removePinBtn, &QPushButton::clicked, this, [table]() {
        auto sel = table->selectionModel()->selectedRows();
        for (int i = sel.count()-1; i >=0; --i) table->removeRow(sel.at(i).row());
    });

        connect(jsonBtn, &QPushButton::clicked, this, [this, table, boardEdit, coreCombo, widthSpin, heightSpin, pitchSpin]() {
                QDialog d(this);
                d.setWindowTitle("JSON: Template / Importar");
                d.setMinimumSize(640, 480);
                d.setStyleSheet(
                    "QDialog { background: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 12px; }"
                    "QPlainTextEdit { background: #FFFFFF; border: 1px solid #CBD5E1; border-radius: 8px; color: #0F172A; font-family: Consolas, 'Courier New', monospace; font-size: 11px; padding: 8px; }"
                    "QPlainTextEdit:focus { border-color: #3B82F6; }"
                    "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: #FFFFFF; padding: 8px 16px; font-weight: 600; font-size: 12px; }"
                    "QPushButton:hover { background: #1D4ED8; }"
                    "QPushButton:pressed { background: #1E40AF; }"
                    "QPushButton#cancel { background: #F1F5F9; border: 1px solid #E2E8F0; color: #475569; }"
                    "QPushButton#cancel:hover { background: #E2E8F0; color: #0F172A; }"
                );
                auto* lay = new QVBoxLayout(&d);
                auto* txt = new QPlainTextEdit(&d);
                // Template provided by user
                QString tmpl = R"({
    "name": "ESP32-WROOM-32",
    "manufacturer": "Espressif",
    "package": {
        "type": "DIP-38",
        "pins": 38,
        "width": 15.2,
        "height": 25.5
    },

    "layout": {
        "left": [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18],
        "right": [19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36]
    },

    "pins": [
        { "physicalPin": 1, "name": "3V3", "side": "left", "position": 0, "type": "power", "functions": ["POWER_3V3"], "voltage": 3.3 },
        { "physicalPin": 2, "name": "EN", "side": "left", "position": 1, "type": "control", "functions": ["ENABLE","RESET"] },
        { "physicalPin": 3, "name": "GPIO36", "side": "left", "position": 2, "gpio": 36, "type": "io", "adc": {"unit":1,"channel":0}, "inputOnly": true, "functions": ["ADC1_CH0"] },
        { "physicalPin": 4, "name": "GPIO39", "side": "left", "position": 3, "gpio": 39, "type": "io", "adc": {"unit":1,"channel":3}, "inputOnly": true, "functions": ["ADC1_CH3"] }
    ],

    "interfaces": {
        "uart": [ { "name": "UART0", "tx": "GPIO1", "rx": "GPIO3" } ],
        "i2c": [ { "name": "I2C0", "sda": "GPIO21", "scl": "GPIO22" } ],
        "spi": [ { "name": "VSPI", "mosi": "GPIO23", "miso": "GPIO19", "sck": "GPIO18", "cs": "GPIO5" } ]
    }
})";
                txt->setPlainText(tmpl);
                lay->addWidget(txt);
                auto* btnLay = new QHBoxLayout();
                btnLay->addStretch();
                auto* cancel = new QPushButton("Cancelar"); cancel->setObjectName("cancel");
                auto* apply = new QPushButton("Aplicar JSON");
                btnLay->addWidget(cancel);
                btnLay->addWidget(apply);
                lay->addLayout(btnLay);
                connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
                connect(apply, &QPushButton::clicked, &d, &QDialog::accept);
                if (d.exec() != QDialog::Accepted) return;
                QString js = txt->toPlainText();
                QJsonParseError perr;
                QJsonDocument jd = QJsonDocument::fromJson(js.toUtf8(), &perr);
                if (perr.error != QJsonParseError::NoError || !jd.isObject()) {
                        QMessageBox::warning(this, "JSON inválido", "O JSON informado é inválido: " + perr.errorString());
                        return;
                }
                QJsonObject jo = jd.object();
                if (jo.contains("name")) boardEdit->setText(jo.value("name").toString());
                if (jo.contains("package") && jo.value("package").isObject()) {
                        QJsonObject pkg = jo.value("package").toObject();
                        if (pkg.contains("width")) widthSpin->setValue(pkg.value("width").toDouble(widthSpin->value()));
                        if (pkg.contains("height")) heightSpin->setValue(pkg.value("height").toDouble(heightSpin->value()));
                }
                if (jo.contains("pins") && jo.value("pins").isArray()) {
                        table->setRowCount(0);
                        QJsonArray parr = jo.value("pins").toArray();
                        for (const auto& pv : parr) {
                                if (!pv.isObject()) continue;
                                QJsonObject pj = pv.toObject();
                                int r = table->rowCount(); table->insertRow(r);
                                table->setItem(r, 0, new QTableWidgetItem(pj.value("name").toString()));
                                table->setItem(r, 1, new QTableWidgetItem(pj.value("physicalPin").toVariant().toString()));
                                table->setItem(r, 2, new QTableWidgetItem(pj.value("type").toString()));
                                if (pj.contains("side")) table->item(r,0)->setData(Qt::UserRole, pj.value("side").toString());
                        }
                }
        });

    // AI fill removed — user opted out

    // Action buttons
    auto* actionLay = new QHBoxLayout();
    actionLay->addStretch();
    auto* cancelBtn = new QPushButton("Cancelar"); cancelBtn->setObjectName("cancelBtn");
    auto* saveBtn = new QPushButton("Salvar");
    actionLay->addWidget(cancelBtn);
    actionLay->addWidget(saveBtn);
    mainLayout->addLayout(actionLay);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted) return;

    QJsonObject cfg;
    cfg["board"] = boardEdit->text();
    cfg["core"] = coreCombo->currentText();
    QJsonObject bsize;
    bsize["width_mm"] = widthSpin->value();
    bsize["height_mm"] = heightSpin->value();
    cfg["board_size"] = bsize;
    cfg["pin_pitch_mm"] = pitchSpin->value();
    QJsonArray pa;
    for (int r=0; r < table->rowCount(); ++r) {
        QJsonObject pj;
        pj["name"] = table->item(r,0) ? table->item(r,0)->text() : QString();
        pj["pin"] = table->item(r,1) ? table->item(r,1)->text() : QString();
        pj["role"] = table->item(r,2) ? table->item(r,2)->text() : QString();
        QVariant sidev;
        if (table->item(r,0)) sidev = table->item(r,0)->data(Qt::UserRole);
        if (sidev.isValid()) pj["side"] = sidev.toString();
        pa.append(pj);
    }
    cfg["pins"] = pa;
    cfg["pin_count"] = pa.size();

    QJsonDocument doc(cfg);
    comp->setProperty("microcontrollerConfig", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    if (comp->componentType() == "esp32") {
        comp->applyMicrocontrollerConfig(cfg);
    }
    statusBar()->showMessage(QString("Configurações de microcontrolador atualizadas para %1").arg(comp->name()), 3000);
    logMessage(QString("Microcontroller %1 config saved: %2").arg(comp->id()).arg(QString::fromUtf8(doc.toJson(QJsonDocument::Compact))), "SUCCESS");
}








// AI suggestion implementation removed.


void MainWindow::logMessage(const QString& message, const QString& type) {
    QString timeStr = QTime::currentTime().toString("hh:mm:ss");
    QString color = "#64748B"; // gray for INFO
    if (type == "SUCCESS") color = "#10B981"; // green
    else if (type == "WARNING") color = "#F59E0B"; // orange
    else if (type == "ERROR") color = "#EF4444"; // red
    else if (type == "SYSTEM") color = "#6366F1"; // indigo/blue

    QString logLine = QString("<span style='color: #475569;'>[%1]</span> <span style='color: %2; font-weight: bold;'>[%3]</span> <span style='color: #0F172A;'>%4</span>")
                      .arg(timeStr, color, type, message);
    
    m_compilerConsole->appendHtml(logLine);
}

void MainWindow::viewCompiledCodeModal() {
    QDialog dialog(this);
    dialog.setWindowTitle("Código C++ Gerado - IDE Embedded");
    dialog.resize(800, 600);
    dialog.setStyleSheet("background-color: #FBFBFB; color: #0F172A;");

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto* titleLabel = new QLabel("Código C++ / Arduino Gerado em Tempo Real", &dialog);
    titleLabel->setStyleSheet("color: #1D4ED8; font-size: 14px; font-weight: bold;");
    layout->addWidget(titleLabel);

    // Auto-atualiza o código antes de mostrar
    synchronizeLoopBlocks();
    m_compiledCode = CodeGenerator::generateArduinoCode(
        m_scene->components(),
        m_scene->cables(),
        m_blockEditor->getEventBlockStorage(),
        m_webPageData
    );

    // Abas
    auto* tabWidget = new QTabWidget(&dialog);
    tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #E2E8F0; border-radius: 4px; background: #FFFFFF; } "
        "QTabBar::tab { background: #E2E8F0; padding: 8px 16px; margin-right: 2px; color: #475569; border-top-left-radius: 4px; border-top-right-radius: 4px; } "
        "QTabBar::tab:selected { background: #FFFFFF; font-weight: bold; color: #1D4ED8; border: 1px solid #E2E8F0; border-bottom: none; }"
    );
    layout->addWidget(tabWidget);

    // Aba 1: Código Padrão
    auto* tabOriginal = new QWidget();
    auto* layoutOriginal = new QVBoxLayout(tabOriginal);
    layoutOriginal->setContentsMargins(12, 12, 12, 12);
    
    auto* codeEditorOriginal = new QPlainTextEdit(tabOriginal);
    codeEditorOriginal->setReadOnly(true);
    codeEditorOriginal->setPlainText(m_compiledCode.isEmpty() ? "// Nenhum código gerado ainda. Monte um circuito para gerar." : m_compiledCode);
    QString editorStyle = 
        "QPlainTextEdit { "
        "  background-color: #FAFAFA; "
        "  border: 1px solid #E2E8F0; "
        "  border-radius: 4px; "
        "  color: #0F172A; "
        "  font-family: 'Fira Code', 'Consolas', 'Courier New', monospace; "
        "  font-size: 13px; "
        "  padding: 12px; "
        "  line-height: 1.5; "
        "}";
    codeEditorOriginal->setStyleSheet(editorStyle);
    layoutOriginal->addWidget(codeEditorOriginal);
    tabWidget->addTab(tabOriginal, "Código Padrão");

    // Aba 2: Código Otimizado (Copilot)
    auto* tabCopilot = new QWidget();
    auto* layoutCopilot = new QVBoxLayout(tabCopilot);
    layoutCopilot->setContentsMargins(12, 12, 12, 12);

    auto* copilotLayout = new QHBoxLayout();
    auto* copilotLabel = new QLabel("Ações de IA:", tabCopilot);
    copilotLabel->setStyleSheet("color: #6366F1; font-weight: bold;");
    copilotLayout->addWidget(copilotLabel);
    
    auto* btnOptPerf = new QPushButton("Otimizar Memória/CPU", tabCopilot);
    auto* btnOptSize = new QPushButton("Reduzir Tamanho", tabCopilot);
    auto* btnRust = new QPushButton("Emular Rust", tabCopilot);
    auto* btnPython = new QPushButton("MicroPython", tabCopilot);

    QString copilotBtnStyle = 
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #E2F0FE, stop:0.46 #B8DCFF, stop:1 #8BBFFF); "
        "  border: 1px solid #7FA2C9; "
        "  border-radius: 6px; "
        "  padding: 6px 12px; "
        "  color: #0369A1; "
        "  font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F0F8FF, stop:0.46 #D9EDFF, stop:1 #B8DCFF); "
        "} "
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8BBFFF, stop:1 #B8DCFF); "
        "} ";
    btnOptPerf->setStyleSheet(copilotBtnStyle);
    btnOptSize->setStyleSheet(copilotBtnStyle);
    btnRust->setStyleSheet(copilotBtnStyle);
    btnPython->setStyleSheet(copilotBtnStyle);

    copilotLayout->addWidget(btnOptPerf);
    copilotLayout->addWidget(btnOptSize);
    copilotLayout->addWidget(btnRust);
    copilotLayout->addWidget(btnPython);
    copilotLayout->addStretch();
    layoutCopilot->addLayout(copilotLayout);

    auto* codeEditorCopilot = new QPlainTextEdit(tabCopilot);
    codeEditorCopilot->setReadOnly(true);
    codeEditorCopilot->setPlainText("// Clique em uma das ações de IA acima para otimizar ou traduzir o código.");
    codeEditorCopilot->setStyleSheet(editorStyle);
    layoutCopilot->addWidget(codeEditorCopilot);
    tabWidget->addTab(tabCopilot, "Código Otimizado (Copilot)");

    auto executeOpt = [this, codeEditorCopilot](AiOptimizer::OptimizeMode mode) {
        if (m_aiOptimizer->getApiKey().isEmpty()) {
            QMessageBox::warning(codeEditorCopilot, "Copilot", "Configure sua API Key do Google Gemini no menu Ajustes -> Configurar Copilot.");
            return;
        }
        codeEditorCopilot->setPlainText("// O Copilot está analisando e traduzindo seu código... Aguarde uns segundos...\n");
        
        auto conn1 = std::make_shared<QMetaObject::Connection>();
        auto conn2 = std::make_shared<QMetaObject::Connection>();
        
        *conn1 = connect(m_aiOptimizer, &AiOptimizer::optimizationFinished, codeEditorCopilot, [codeEditorCopilot, conn1, conn2](const QString& res) {
            codeEditorCopilot->setPlainText(res);
            disconnect(*conn1); disconnect(*conn2);
        });
        *conn2 = connect(m_aiOptimizer, &AiOptimizer::optimizationError, codeEditorCopilot, [codeEditorCopilot, conn1, conn2](const QString& err) {
            codeEditorCopilot->setPlainText("// Erro na IA:\n" + err);
            disconnect(*conn1); disconnect(*conn2);
        });
        m_aiOptimizer->optimizeCode(m_compiledCode, mode);
    };

    connect(btnOptPerf, &QPushButton::clicked, [&]() { executeOpt(AiOptimizer::OptimizePerformance); });
    connect(btnOptSize, &QPushButton::clicked, [&]() { executeOpt(AiOptimizer::ReduceSize); });
    connect(btnRust, &QPushButton::clicked, [&]() { executeOpt(AiOptimizer::TranslateToRust); });
    connect(btnPython, &QPushButton::clicked, [&]() { executeOpt(AiOptimizer::TranslateToPython); });

    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);

    auto* copyButton = new QPushButton("Copiar Código", &dialog);
    copyButton->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #D1FAE5, stop:0.45 #A7F3D0, stop:0.46 #34D399, stop:1 #10B981); "
        "  border: 1px solid #059669; "
        "  border-radius: 6px; "
        "  padding: 8px 16px; "
        "  font-weight: bold; "
        "  color: #064E3B; "
        "  font-size: 12px; "
        "}"
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ECFDF5, stop:0.45 #D1FAE5, stop:0.46 #6EE7B7, stop:1 #34D399); "
        "}"
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #10B981, stop:1 #34D399); "
        "}"
    );
    connect(copyButton, &QPushButton::clicked, this, [tabWidget, codeEditorOriginal, codeEditorCopilot, copyButton]() {
        if (tabWidget->currentIndex() == 0) {
            QGuiApplication::clipboard()->setText(codeEditorOriginal->toPlainText());
        } else {
            QGuiApplication::clipboard()->setText(codeEditorCopilot->toPlainText());
        }
        copyButton->setText("Copiado!");
        copyButton->setStyleSheet(
            "QPushButton { "
            "  background: #312E81; "
            "  border: 1px solid #4F46E5; "
            "  border-radius: 6px; "
            "  padding: 8px 16px; "
            "  font-weight: bold; "
            "  color: #38BDF8; "
            "  font-size: 12px; "
            "}"
        );
        QTimer::singleShot(2000, copyButton, [copyButton]() {
            copyButton->setText("Copiar Código");
            copyButton->setStyleSheet(
                "QPushButton { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #D1FAE5, stop:0.45 #A7F3D0, stop:0.46 #34D399, stop:1 #10B981); "
                "  border: 1px solid #059669; "
                "  border-radius: 6px; "
                "  padding: 8px 16px; "
                "  font-weight: bold; "
                "  color: #064E3B; "
                "  font-size: 12px; "
                "}"
                "QPushButton:hover { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ECFDF5, stop:0.45 #D1FAE5, stop:0.46 #6EE7B7, stop:1 #34D399); "
                "}"
                "QPushButton:pressed { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #10B981, stop:1 #34D399); "
                "}"
            );
        });
    });
    buttonLayout->addWidget(copyButton);

    buttonLayout->addStretch();

    auto* closeButton = new QPushButton("Fechar", &dialog);
    closeButton->setStyleSheet(
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F1F5F9, stop:0.45 #E2E8F0, stop:0.46 #CBD5E1, stop:1 #94A3B8); "
        "  border: 1px solid #64748B; "
        "  border-radius: 6px; "
        "  padding: 8px 16px; "
        "  font-weight: bold; "
        "  color: #0F172A; "
        "  font-size: 12px; "
        "}"
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F8FAFC, stop:0.45 #F1F5F9, stop:0.46 #E2E8F0, stop:1 #CBD5E1); "
        "}"
        "QPushButton:pressed { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #94A3B8, stop:1 #CBD5E1); "
        "}"
    );
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonLayout->addWidget(closeButton);

    layout->addLayout(buttonLayout);

    dialog.exec();
}

void MainWindow::showFirmwareInfo() {
    QDialog dialog(this);
    dialog.setWindowTitle("Ajuda e Documentação — IDE Embedded");
    dialog.resize(880, 780);
    dialog.setStyleSheet(
        "QDialog { "
        "  background-color: #FFFFFF; "
        "  border: 1px solid #CBD5E1; "
        "  border-radius: 8px; "
        "}"
        "QLabel { font-family: 'Segoe UI', Arial, sans-serif; color: #1E293B; }"
        "QTabWidget::pane { "
        "  border: 1px solid #CBD5E1; "
        "  background: #FFFFFF; "
        "  border-radius: 6px; "
        "  padding: 8px; "
        "}"
        "QTabBar { background: #EAF0F6; }"
        "QTabBar::tab { "
        "  background: #E2E8F0; "
        "  color: #475569; "
        "  border: 1px solid #CBD5E1; "
        "  border-bottom: none; "
        "  border-top-left-radius: 6px; "
        "  border-top-right-radius: 6px; "
        "  padding: 8px 16px; "
        "  font-size: 11px; "
        "  font-weight: 600; "
        "  font-family: 'Segoe UI', Arial; "
        "  margin-right: 2px; "
        "}"
        "QTabBar::tab:selected { "
        "  background: #FFFFFF; "
        "  color: #2563EB; "
        "  border-color: #CBD5E1; "
        "  border-bottom: 1px solid #FFFFFF; "
        "}"
        "QTabBar::tab:hover { "
        "  color: #2563EB; "
        "  background: #F1F5F9; "
        "}"
        "QPushButton { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #60A5FA, stop:0.4 #3B82F6, stop:0.5 #2563EB, stop:1 #1D4ED8); "
        "  border: 1.5px solid rgba(255, 255, 255, 0.7); "
        "  border-radius: 6px; "
        "  color: #FFFFFF; "
        "  padding: 10px 18px; "
        "  font-weight: bold; "
        "  font-size: 12px; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "}"
        "QPushButton:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #93C5FD, stop:0.3 #60A5FA, stop:0.6 #3B82F6, stop:1 #1E40AF); "
        "  border-color: #FFFFFF; "
        "}"
        "QPushButton#sec { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FFFFFF, stop:0.4 #F1F5F9, stop:0.5 #E2E8F0, stop:1 #CBD5E1); "
        "  border: 1px solid #94A3B8; "
        "  border-radius: 6px; "
        "  color: #334155; "
        "  padding: 10px 18px; "
        "  font-weight: bold; "
        "  font-size: 12px; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "}"
        "QPushButton#sec:hover { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FFFFFF, stop:0.3 #F8FAFC, stop:0.6 #E2E8F0, stop:1 #94A3B8); "
        "  border-color: #64748B; "
        "  color: #0F172A; "
        "}"
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* header = new QLabel("Central de Ajuda e Documentação", &dialog);
    header->setStyleSheet("font-size: 16px; font-weight: 800; color: #0F172A;");
    layout->addWidget(header);

    auto* tabs = new QTabWidget(&dialog);

    // ─────────────────────────────────────────────────────────────────────────
    // TAB 0: TUTORIAL INTERATIVO (LAUNCHER)
    // ─────────────────────────────────────────────────────────────────────────
    {
        auto* tutWidget = new QWidget();
        tutWidget->setStyleSheet("QWidget { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #F0F9FF, stop:1 #E0F2FE); }");
        auto* tutLayout = new QVBoxLayout(tutWidget);
        tutLayout->setContentsMargins(30, 20, 30, 20);
        tutLayout->setSpacing(10);
        tutLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

        auto* title = new QLabel("Selecione um Tutorial Interativo", tutWidget);
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet("font-size: 24px; font-weight: 900; color: #0284C7; margin-bottom: 2px; background: transparent;");
        tutLayout->addWidget(title);

        auto* desc = new QLabel(
            "Aprenda a usar a IDE com guias práticos passo a passo.\n"
            "O sistema escurecerá a tela e mostrará exatamente onde você deve clicar.", tutWidget);
        desc->setAlignment(Qt::AlignCenter);
        desc->setStyleSheet("font-size: 13px; color: #334155; margin-bottom: 8px; background: transparent;");
        tutLayout->addWidget(desc);

        // --- Card 1: LED & Button ---
        auto* card1 = new QFrame(tutWidget);
        card1->setFixedSize(300, 240);
        card1->setStyleSheet(
            "QFrame { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.4 #F0FDFA, stop:1 #CCFBF1); "
            "  border: 1px solid #5EEAD4; "
            "  border-radius: 12px; "
            "}"
        );
        auto* l1 = new QVBoxLayout(card1);
        l1->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        l1->setContentsMargins(20, 20, 20, 20);
        
        auto* t1 = new QLabel("Tutorial 1\nLED e Botão", card1);
        t1->setAlignment(Qt::AlignCenter);
        t1->setStyleSheet("font-size: 18px; font-weight: 800; color: #0F766E; border: none; background: transparent;");
        l1->addWidget(t1);
        
        auto* d1 = new QLabel("Acenda um LED usando um botão e veja o simulador em ação. Perfeito para começar!", card1);
        d1->setAlignment(Qt::AlignCenter);
        d1->setWordWrap(true);
        d1->setStyleSheet("font-size: 13px; color: #334155; border: none; background: transparent; margin-top: 10px;");
        l1->addWidget(d1);
        
        l1->addStretch();
        auto* btnStart1 = new QPushButton("Iniciar Tutorial 1", card1);
        btnStart1->setCursor(Qt::PointingHandCursor);
        btnStart1->setFixedSize(200, 42);
        btnStart1->setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #34D399, stop:0.4 #10B981, stop:0.5 #059669, stop:1 #047857); "
            "  border: 1.5px solid rgba(255, 255, 255, 0.85); border-radius: 6px; color: white; font-weight: bold; font-size: 13px; "
            "}"
            "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6EE7B7, stop:0.3 #34D399, stop:0.6 #10B981, stop:1 #065F46); }"
        );
        l1->addWidget(btnStart1, 0, Qt::AlignHCenter);

        // --- Card 2: HC-SR04 & Buzzer ---
        auto* card2 = new QFrame(tutWidget);
        card2->setFixedSize(300, 240);
        card2->setStyleSheet(
            "QFrame { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.4 #FAF5FF, stop:1 #E9D5FF); "
            "  border: 1px solid #D8B4FE; "
            "  border-radius: 12px; "
            "}"
        );
        auto* l2 = new QVBoxLayout(card2);
        l2->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        l2->setContentsMargins(20, 20, 20, 20);
        
        auto* t2 = new QLabel("Tutorial 2\nSensor Ultrassônico", card2);
        t2->setAlignment(Qt::AlignCenter);
        t2->setStyleSheet("font-size: 18px; font-weight: 800; color: #6B21A8; border: none; background: transparent;");
        l2->addWidget(t2);
        
        auto* d2 = new QLabel("Crie um radar sonoro com HC-SR04 e controle os bips do Buzzer pela distância em tempo real.", card2);
        d2->setAlignment(Qt::AlignCenter);
        d2->setWordWrap(true);
        d2->setStyleSheet("font-size: 13px; color: #334155; border: none; background: transparent; margin-top: 10px;");
        l2->addWidget(d2);
        
        l2->addStretch();
        auto* btnStart2 = new QPushButton("Iniciar Tutorial 2", card2);
        btnStart2->setCursor(Qt::PointingHandCursor);
        btnStart2->setFixedSize(200, 42);
        btnStart2->setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #C084FC, stop:0.4 #A855F7, stop:0.5 #9333EA, stop:1 #7E22CE); "
            "  border: 1.5px solid rgba(255, 255, 255, 0.85); border-radius: 6px; color: white; font-weight: bold; font-size: 13px; "
            "}"
            "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #DDD6FE, stop:0.3 #C084FC, stop:0.6 #A855F7, stop:1 #6B21A8); }"
        );
        l2->addWidget(btnStart2, 0, Qt::AlignHCenter);

        // --- Card 3: Motor & IoT ---
        auto* card3 = new QFrame(tutWidget);
        card3->setFixedSize(300, 240);
        card3->setStyleSheet(
            "QFrame { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.4 #FFFBEB, stop:1 #FEF3C7); "
            "  border: 1px solid #FCD34D; "
            "  border-radius: 12px; "
            "}"
        );
        auto* l3 = new QVBoxLayout(card3);
        l3->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        l3->setContentsMargins(20, 20, 20, 20);
        
        auto* t3 = new QLabel("Tutorial 3\nMotor IoT", card3);
        t3->setAlignment(Qt::AlignCenter);
        t3->setStyleSheet("font-size: 18px; font-weight: 800; color: #D97706; border: none; background: transparent;");
        l3->addWidget(t3);
        
        auto* d3 = new QLabel("Crie um Dashboard Web (site) para controlar a posição de um Servomotor via Wi-Fi.", card3);
        d3->setAlignment(Qt::AlignCenter);
        d3->setWordWrap(true);
        d3->setStyleSheet("font-size: 13px; color: #334155; border: none; background: transparent; margin-top: 10px;");
        l3->addWidget(d3);
        
        l3->addStretch();
        auto* btnStart3 = new QPushButton("Iniciar Tutorial 3", card3);
        btnStart3->setCursor(Qt::PointingHandCursor);
        btnStart3->setFixedSize(200, 42);
        btnStart3->setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FBBF24, stop:0.4 #F59E0B, stop:0.5 #D97706, stop:1 #B45309); "
            "  border: 1.5px solid rgba(255, 255, 255, 0.85); border-radius: 6px; color: white; font-weight: bold; font-size: 13px; "
            "}"
            "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FDE68A, stop:0.3 #FBBF24, stop:0.6 #F59E0B, stop:1 #D97706); }"
        );
        l3->addWidget(btnStart3, 0, Qt::AlignHCenter);

        // --- Card 4: LDR & Relay (Luz Automática) ---
        auto* card4 = new QFrame(tutWidget);
        card4->setFixedSize(300, 240);
        card4->setStyleSheet(
            "QFrame { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.4 #F0FDF4, stop:1 #DCFCE7); "
            "  border: 1px solid #86EFAC; "
            "  border-radius: 12px; "
            "}"
        );
        auto* l4 = new QVBoxLayout(card4);
        l4->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        l4->setContentsMargins(20, 20, 20, 20);
        
        auto* t4 = new QLabel("Tutorial 4\nLuz Automática", card4);
        t4->setAlignment(Qt::AlignCenter);
        t4->setStyleSheet("font-size: 18px; font-weight: 800; color: #166534; border: none; background: transparent;");
        l4->addWidget(t4);
        
        auto* d4 = new QLabel("Use um sensor de luz LDR para ligar uma lâmpada através de um módulo de Relé sob pouca luz.", card4);
        d4->setAlignment(Qt::AlignCenter);
        d4->setWordWrap(true);
        d4->setStyleSheet("font-size: 13px; color: #334155; border: none; background: transparent; margin-top: 10px;");
        l4->addWidget(d4);
        
        l4->addStretch();
        auto* btnStart4 = new QPushButton("Iniciar Tutorial 4", card4);
        btnStart4->setCursor(Qt::PointingHandCursor);
        btnStart4->setFixedSize(200, 42);
        btnStart4->setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4ADE80, stop:0.4 #22C55E, stop:0.5 #16A34A, stop:1 #15803D); "
            "  border: 1.5px solid rgba(255, 255, 255, 0.85); border-radius: 6px; color: white; font-weight: bold; font-size: 13px; "
            "}"
            "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #86EFAC, stop:0.3 #4ADE80, stop:0.6 #22C55E, stop:1 #166534); }"
        );
        l4->addWidget(btnStart4, 0, Qt::AlignHCenter);

        // Tutorial Cards Layout (2x2 Grid)
        auto* cardsVLayout = new QVBoxLayout();
        cardsVLayout->setSpacing(15);
        
        auto* row1Layout = new QHBoxLayout();
        row1Layout->setSpacing(25);
        row1Layout->setAlignment(Qt::AlignCenter);
        row1Layout->addWidget(card1);
        row1Layout->addWidget(card2);

        auto* row2Layout = new QHBoxLayout();
        row2Layout->setSpacing(25);
        row2Layout->setAlignment(Qt::AlignCenter);
        row2Layout->addWidget(card3);
        row2Layout->addWidget(card4);

        cardsVLayout->addLayout(row1Layout);
        cardsVLayout->addLayout(row2Layout);

        tutLayout->addLayout(cardsVLayout);
        tutLayout->addStretch();

        QDialog* dlg = &dialog;
        connect(btnStart1, &QPushButton::clicked, this, [this, dlg]() {
            dlg->accept();
            QTimer::singleShot(300, this, &MainWindow::startInteractiveTutorial);
        });
        connect(btnStart2, &QPushButton::clicked, this, [this, dlg]() {
            dlg->accept();
            QTimer::singleShot(300, this, &MainWindow::startDistanceSensorTutorial);
        });
        connect(btnStart3, &QPushButton::clicked, this, [this, dlg]() {
            dlg->accept();
            QTimer::singleShot(300, this, &MainWindow::startMotorIoTTutorial);
        });
        connect(btnStart4, &QPushButton::clicked, this, [this, dlg]() {
            dlg->accept();
            QTimer::singleShot(300, this, &MainWindow::startLdrRelayTutorial);
        });

        tabs->addTab(tutWidget, "Tutoriais Interativos");
    }


    // ─────────────────────────────────────────────────────────────────────────
    // TAB 1: CONTROLES DA IDE
    // ─────────────────────────────────────────────────────────────────────────
    auto* shortcutsWidget = new QWidget();
    auto* shortcutsLayout = new QVBoxLayout(shortcutsWidget);
    shortcutsLayout->setContentsMargins(16, 16, 16, 16);
    shortcutsLayout->setSpacing(10);

    auto* shortcutsIntro = new QLabel("Guia de Operação e Controles da Interface", shortcutsWidget);
    shortcutsIntro->setStyleSheet("font-size: 13px; font-weight: bold; color: #0F172A;");
    shortcutsLayout->addWidget(shortcutsIntro);

    auto* shortcutsTable = new QTableWidget(shortcutsWidget);
    shortcutsTable->setColumnCount(3);
    shortcutsTable->setHorizontalHeaderLabels({"Ação / Atalho", "Contexto", "Descrição do Comando"});
    shortcutsTable->setAlternatingRowColors(true);
    shortcutsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shortcutsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    shortcutsTable->setStyleSheet(
        "QTableWidget { background-color: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 8px; gridline-color: #F1F5F9; }"
        "QHeaderView::section { background-color: #F8FAFC; border: 1px solid #E2E8F0; padding: 6px; font-weight: bold; color: #475569; }"
        "QTableWidget::item { padding: 8px; font-size: 11px; }"
    );

    struct ShortcutInfo {
        QString gesture;
        QString context;
        QString description;
    };
    QVector<ShortcutInfo> shortcuts = {
        {"Duplo clique", "Área de Trabalho", "Ao clicar 2 vezes na área de trabalho, irá aparecer o menu de componentes."},
        {"Pressionar DEL", "Área de Trabalho / Seleção", "Ao pressionar DEL, o componente selecionado será deletado."},
        {"Botão direito do mouse", "Sobre o Componente", "Ao clicar com o botão direito no componente, os eventos serão exibidos."},
        {"Duplo clique", "Área de Eventos", "Ao entrar em um evento e clicar 2 vezes na área de eventos, aparecerá o menu de código."},
        {"Duplo clique", "Bloco de Código", "Dando 2 cliques em um bloco de código, ele será deletado."},
        {"Clique esquerdo", "Roteamento de PCB (Trilhas)", "Quando estiver fazendo uma trilha, clicar com o botão esquerdo prende a trilha para conseguir movê-la para outro lado."},
        {"Botão direito do mouse", "Roteamento de PCB (Trilhas)", "Caso erre a trilha, clique com o botão direito do mouse para desfazer a trilha e soltá-la do ponteiro."},
        {"Clique em trilha conectada", "Roteamento de PCB (Trilhas)", "Caso veja trilhas como VCC que já podem se conectar, basta clicar nela para prender a trilha atual na que já está conectada ao VCC."}
    };

    shortcutsTable->setRowCount(shortcuts.size());
    for (int i = 0; i < shortcuts.size(); ++i) {
        const auto& item = shortcuts[i];

        auto* itemGesture = new QTableWidgetItem(item.gesture);
        itemGesture->setFont(QFont("Segoe UI", 9, QFont::Bold));
        itemGesture->setForeground(QColor("#2563EB"));

        auto* itemContext = new QTableWidgetItem(item.context);
        itemContext->setFont(QFont("Segoe UI", 9, QFont::Bold));
        itemContext->setForeground(QColor("#475569"));

        auto* itemDesc = new QTableWidgetItem(item.description);
        itemDesc->setFont(QFont("Segoe UI", 9));
        itemDesc->setForeground(QColor("#0F172A"));

        shortcutsTable->setItem(i, 0, itemGesture);
        shortcutsTable->setItem(i, 1, itemContext);
        shortcutsTable->setItem(i, 2, itemDesc);
    }

    shortcutsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    shortcutsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    shortcutsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    shortcutsLayout->addWidget(shortcutsTable);
    tabs->addTab(shortcutsWidget, "Controles da IDE");



    // ─────────────────────────────────────────────────────────────────────────
    // TAB 3: LICENÇA (Side-by-Side)
    // ─────────────────────────────────────────────────────────────────────────
    auto* licWidget = new QWidget();
    auto* licLayout = new QVBoxLayout(licWidget);
    licLayout->setContentsMargins(16, 16, 16, 16);
    licLayout->setSpacing(10);

    auto* licTitle = new QLabel("Termos de Uso e Licenciamento Acadêmico/Educacional", licWidget);
    licTitle->setStyleSheet("font-size: 13px; font-weight: bold; color: #0F172A;");
    licLayout->addWidget(licTitle);

    auto* licHLayout = new QHBoxLayout();
    licHLayout->setSpacing(14);

    // Left Column: Visual summary (No emojis)
    auto* summaryFrame = new QFrame(licWidget);
    summaryFrame->setFixedWidth(250);
    summaryFrame->setStyleSheet(
        "QFrame { background-color: #F8FAFC; border: 1px solid #E2E8F0; border-radius: 8px; }"
        "QLabel#sect { font-weight: 800; font-size: 10px; text-transform: uppercase; color: #475569; margin-top: 6px; }"
        "QLabel#item { font-size: 11px; color: #334155; font-weight: 600; }"
    );
    auto* sLayout = new QVBoxLayout(summaryFrame);
    sLayout->setContentsMargins(14, 14, 14, 14);
    sLayout->setSpacing(8);

    auto* sTitle = new QLabel("Resumo Simplificado", summaryFrame);
    sTitle->setStyleSheet("font-size: 12px; font-weight: bold; color: #0F172A; margin-bottom: 2px;");
    sLayout->addWidget(sTitle);

    auto* allowedTitle = new QLabel("O QUE É PERMITIDO:", summaryFrame);
    allowedTitle->setObjectName("sect"); allowedTitle->setStyleSheet("color: #16A34A;"); sLayout->addWidget(allowedTitle);
    auto* a1 = new QLabel("• Estudos e uso acadêmico", summaryFrame); a1->setObjectName("item"); sLayout->addWidget(a1);
    auto* a2 = new QLabel("• Modificações para uso pessoal", summaryFrame); a2->setObjectName("item"); sLayout->addWidget(a2);
    auto* a3 = new QLabel("• Compartilhamento educacional", summaryFrame); a3->setObjectName("item"); sLayout->addWidget(a3);

    auto* forbiddenTitle = new QLabel("O QUE É PROIBIDO:", summaryFrame);
    forbiddenTitle->setObjectName("sect"); forbiddenTitle->setStyleSheet("color: #DC2626;"); sLayout->addWidget(forbiddenTitle);
    auto* f1 = new QLabel("• Comercialização do software", summaryFrame); f1->setObjectName("item"); sLayout->addWidget(f1);
    auto* f2 = new QLabel("• Uso em produtos de lucro", summaryFrame); f2->setObjectName("item"); sLayout->addWidget(f2);
    auto* f3 = new QLabel("• Distribuição comercial", summaryFrame); f3->setObjectName("item"); sLayout->addWidget(f3);

    auto* reqTitle = new QLabel("OBRIGAÇÕES:", summaryFrame);
    reqTitle->setObjectName("sect"); reqTitle->setStyleSheet("color: #2563EB;"); sLayout->addWidget(reqTitle);
    auto* r1 = new QLabel("• Atribuir autoria a Herick B. Tiburski", summaryFrame); r1->setObjectName("item"); sLayout->addWidget(r1);
    auto* r2 = new QLabel("• Reconhecer o pioneirismo", summaryFrame); r2->setObjectName("item"); sLayout->addWidget(r2);

    sLayout->addStretch();
    licHLayout->addWidget(summaryFrame);

    // Right Column: License legal text
    auto* licenseEdit = new QPlainTextEdit(licWidget);
    licenseEdit->setReadOnly(true);
    licenseEdit->setStyleSheet(
        "QPlainTextEdit { background-color: #F8FAFC; border: 1px solid #E2E8F0; border-radius: 8px;"
        "  color: #334155; font-family: Consolas, monospace; font-size: 10px; padding: 10px; }"
    );
    
    QString licenseText;
    QFile licenseFile("LICENSE");
    if (licenseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        licenseText = licenseFile.readAll();
    } else {
        licenseText = 
            "Educational and Non-Commercial Research License\n\n"
            "Copyright (c) 2026 Herick B. Tiburski\n"
            "All rights reserved.\n\n"
            "The creator and author of this software, Herick B. Tiburski, is recognized as the pioneer of this style of visual event-oriented programming of components for embedded systems.\n\n"
            "Redistribution and use of this software, with or without modification, are permitted solely for educational, academic, or personal study purposes, provided that the following conditions are met:\n\n"
            "1. Redistributions of source code must retain the above copyright notice, this list of conditions, the pioneer acknowledgement, and the following disclaimer.\n"
            "2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions, the pioneer acknowledgement, and the following disclaimer in the documentation and/or other materials provided with the distribution.\n"
            "3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.\n"
            "4. Commercial use, reproduction, distribution, or licensing of this software, in whole or in part, is strictly prohibited. Commercial use includes, but is not limited to, selling, using this software in a commercial product, using this software for commercial training, or using it in a profit-generating context.";
    }
    licenseEdit->setPlainText(licenseText);
    licHLayout->addWidget(licenseEdit, 1);

    licLayout->addLayout(licHLayout, 1);
    tabs->addTab(licWidget, "Licença");

    // ─────────────────────────────────────────────────────────────────────────
    // TAB 4: SOBRE (No Emojis)
    // ─────────────────────────────────────────────────────────────────────────
    auto* aboutWidget = new QWidget();
    auto* aboutLayout = new QVBoxLayout(aboutWidget);
    aboutLayout->setContentsMargins(20, 20, 20, 20);
    aboutLayout->setSpacing(14);

    auto* aTitle = new QLabel("IDE Embedded", aboutWidget);
    aTitle->setStyleSheet("font-size: 16px; font-weight: 800; color: #1E293B;");
    aboutLayout->addWidget(aTitle);

    QString verStr = QCoreApplication::applicationVersion();
    if (verStr.isEmpty()) verStr = "1.0.0 (estudos)";
    auto* aSub = new QLabel(QString("Versão %1  |  Copyright © 2026 Herick B. Tiburski").arg(verStr), aboutWidget);
    aSub->setStyleSheet("font-size: 11px; color: #64748B; font-weight: 600;");
    aboutLayout->addWidget(aSub);

    auto* aDesc = new QLabel(
        "Uma IDE de modelagem gráfica, simulação em tempo real e geração automatizada de firmware "
        "para microcontroladores ESP32 e Arduino.", aboutWidget);
    aDesc->setWordWrap(true);
    aDesc->setStyleSheet("font-size: 12px; line-height: 1.5; color: #475569;");
    aboutLayout->addWidget(aDesc);

    // Pioneer credit card
    auto* pCard = new QFrame(aboutWidget);
    pCard->setStyleSheet("background-color: #EFF6FF; border: 1px solid #DBEAFE; border-radius: 8px;");
    auto* pcLayout = new QVBoxLayout(pCard);
    pcLayout->setContentsMargins(14, 12, 14, 12);
    auto* pcTitle = new QLabel("Arquitetura Pioneira", pCard);
    pcTitle->setStyleSheet("font-weight: 800; color: #1D4ED8; font-size: 11px; text-transform: uppercase;");
    auto* pcText = new QLabel(
        "Desenvolvida sob o modelo de programação orientada a eventos para embarcados "
        "pioneirizada e criada por <b>Herick B. Tiburski</b>.", pCard);
    pcText->setWordWrap(true);
    pcText->setStyleSheet("font-size: 11px; line-height: 1.4; color: #1E40AF;");
    pcLayout->addWidget(pcTitle);
    pcLayout->addWidget(pcText);
    aboutLayout->addWidget(pCard);

    aboutLayout->addStretch();
    tabs->addTab(aboutWidget, "Sobre");

    layout->addWidget(tabs, 1);

    // Bottom Action Row
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* btnClose = new QPushButton("Fechar", &dialog);
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);

    connect(btnClose, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
}

void MainWindow::startInteractiveTutorial() {
    if (!m_tutorialOverlay) {
        m_tutorialOverlay = new TutorialOverlay(this);
    }

    // Find toolbar widget for build/play actions
    QWidget* buildWidget = nullptr;
    QWidget* playWidget = nullptr;
    for (auto* toolbar : findChildren<QToolBar*>()) {
        if (m_buildAction) buildWidget = toolbar->widgetForAction(m_buildAction);
        if (m_playAction) playWidget = toolbar->widgetForAction(m_playAction);
    }

    QVector<TutorialStep> steps;

    // Step 0: Welcome (no target, center)
    steps.append({
        "Bem-vindo ao IDE Embedded!",
        "Este tutorial vai te guiar passo a passo na criação, conexão e programação de um circuito interativo com LED e Botão!\n\n"
        "A tela vai escurecer e destacar os elementos necessários a cada passo. Siga as orientações e veja a mágica acontecer.",
        "Clique em 'Próximo' para começar!",
        nullptr, QRect(), TutorialStep::None
    });

    // Step 1: Add LED
    steps.append({
        "1. Dê dois cliques no workspace e adicione um LED",
        "Vamos começar adicionando a nossa fonte de luz.\n\n"
        "1. Dê DOIS CLIQUES no workspace (mesa de trabalho).\n"
        "2. Digite \"LED\" no campo de busca e adicione um LED à mesa.",
        "Dê duplo clique no workspace e adicione um LED!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 2: Add Resistor
    steps.append({
        "2. Dê dois cliques no workspace e adicione um Resistor",
        "Agora precisamos de um resistor para proteger o LED (ligar direto queima!).\n\n"
        "1. Dê DOIS CLIQUES no workspace.\n"
        "2. Digite \"resistor\" na busca e adicione-o.",
        "Dê duplo clique no workspace e adicione um Resistor!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 3: Edit Resistor Value
    steps.append({
        "3. Dê dois cliques no Resistor e mude seu valor para 220 Ω",
        "Para proteger o LED, precisamos configurar o resistor para 220 ohms.\n\n"
        "1. Dê DOIS CLIQUES no Resistor no workspace para abrir a janela de edição.\n"
        "2. No campo de valor da resistência, digite 220 ou use o botão de atalho rápido '220 Ω'.\n"
        "3. Clique em 'Salvar'.",
        "Dê dois cliques no Resistor e configure seu valor para 220 Ω!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 4: Connect LED to Resistor
    steps.append({
        "4. Conecte a perna vermelha (anodo) do LED ao Resistor",
        "Conecte a perna do LED ao resistor.\n\n"
        "1. Dê um único clique com o botão esquerdo na perna vermelha do LED (anodo).\n"
        "2. Mova o mouse (sem arrastar) e dê outro clique em uma das pernas do Resistor.",
        "Clique no anodo do LED, mova o mouse e clique no Resistor!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 5: Connect Resistor to GPIO2
    steps.append({
        "5. Conecte a outra perna do Resistor ao pino GPIO2 da ESP32",
        "Ligue o resistor na porta de controle da placa.\n\n"
        "1. Dê um clique na outra perna livre do Resistor.\n"
        "2. Mova o mouse and clique no pino GPIO2 da placa ESP32 para fazer a ligação.",
        "Clique na perna do Resistor, mova o mouse e clique no GPIO2!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 6: Connect LED to GND
    steps.append({
        "6. Conecte a perna cinza (catodo) do LED ao pino GND da ESP32",
        "Feche o circuito de alimentação do LED.\n\n"
        "1. Dê um clique na perna cinza do LED (catodo).\n"
        "2. Mova o mouse e clique em um dos pinos GND da placa ESP32.",
        "Clique no catodo do LED, mova o mouse e clique no GND!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 7: Add Button
    steps.append({
        "7. Dê dois cliques no workspace e adicione um Botão",
        "Agora que o LED está conectado, vamos adicionar um componente de entrada: um botão interativo.\n\n"
        "1. Dê DOIS CLIQUES no workspace.\n"
        "2. Busque por \"botão\" e adicione-o.",
        "Dê duplo clique no workspace e adicione um Botão!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 8: Connect Button to GPIO3
    steps.append({
        "8. Conecte uma das pernas do Botão ao pino GPIO3 da ESP32",
        "Ligue a entrada digital do botão à placa.\n\n"
        "1. Dê um clique em uma das pernas do Botão.\n"
        "2. Mova o mouse e clique no pino GPIO3 da placa ESP32.",
        "Conecte uma das pernas do Botão ao pino GPIO3 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 9: Connect Button to GND
    steps.append({
        "9. Conecte a outra perna do Botão a um pino GND livre da ESP32",
        "Complete a conexão do botão com o terra do circuito.\n\n"
        "1. Dê um clique na outra perna livre do Botão.\n"
        "2. Mova o mouse e clique em um pino GND livre da placa ESP32.",
        "Conecte a outra perna do Botão ao pino GND da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 10: Right Click Button
    steps.append({
        "10. Clique com botão direito no Botão e escolha 'Ao Clicar'",
        "Vamos abrir a área de programação para dar comportamento ao botão.\n\n"
        "1. Clique com o BOTÃO DIREITO sobre o Botão no workspace.\n"
        "2. Selecione o evento \"Ao Clicar\" no menu flutuante.",
        "Clique com o botão direito no Botão e selecione 'Ao Clicar'!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Step 11: Block Editor logic
    steps.append({
        "11. Dê dois cliques no editor à direita e adicione uma Ação",
        "Agora vamos programar a ação do clique no botão.\n\n"
        "1. Dê DOIS CLIQUES no editor de blocos à direita.\n"
        "2. Adicione uma Ação para definir o estado do pino GPIO2 (LED) como LIGADO ou INVERTER.",
        "Adicione os blocos de lógica no editor de eventos!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // Step 12: Drag LED to Action
    steps.append({
        "12. Arraste a variável LED_2 da paleta ao campo Alvo da Ação",
        "Para definir que o LED é o alvo desta ação, arraste-o da paleta à esquerda:\n\n"
        "1. Clique e segure o bloco rosa 'LED_2 [GPIO2]' na paleta de 'PINOS E ATUADORES'.\n"
        "2. Arraste e solte-o no campo 'Alvo (Pino / Var)' do seu bloco de Ação.",
        "Arraste o pino LED_2 para o campo Alvo!",
        nullptr, QRect(), TutorialStep::Right
    });

    // Step 13: Build
    steps.append({
        "13. Clique no botão de Build no topo para compilar",
        "Clique no botão de Build (ícone de ferramentas na barra superior) para compilar a lógica e o hardware do seu circuito.",
        "Clique no botão de Build no topo para compilar!",
        buildWidget, QRect(), TutorialStep::Up
    });

    // Step 14: Play Simulation
    steps.append({
        "14. Clique no botão de Play no topo para iniciar a simulação",
        "Com o código compilado com sucesso, clique no botão de Play no topo para rodar a simulação interativa!",
        "Clique no botão de Play no topo para iniciar a simulação!",
        playWidget, QRect(), TutorialStep::Up
    });

    // Step 15: Done
    steps.append({
        "Parabéns! O circuito está pronto e rodando!",
        "Você acabou de montar e programar seu circuito!\n\n"
        "Experimente clicar no Botão no workspace para ver o LED acendendo em tempo real.\n\n"
        "Divirta-se criando novos projetos eletrônicos!",
        "Clique em 'Concluir' para fechar o tutorial.",
        nullptr, QRect(), TutorialStep::None
    });

    m_activeTutorial = 1;
    m_tutorialOverlay->clearVariableDragSteps();
    m_tutorialOverlay->addVariableDragStep(12, "LED"); // step 12: drag LED variable to Alvo
    m_tutorialOverlay->setSteps(steps);
    m_tutorialOverlay->start();
}

// ─────────────────────────────────────────────────────────────────────────────
// TUTORIAL 2: SENSOR DE DISTÂNCIA HC-SR04 + BUZZER
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::startDistanceSensorTutorial() {
    // Enable multitasking initially for Distance Sensor tutorial so user has to disable it
    m_multitaskingEnabled = true;
    CodeGenerator::setMultitaskingEnabled(true);
    if (m_multitaskAction) {
        m_multitaskAction->setChecked(true);
    }
    compileCode();

    if (!m_tutorialOverlay) {
        m_tutorialOverlay = new TutorialOverlay(this);
    }

    // Find toolbar widgets
    QWidget* buildWidget = nullptr;
    QWidget* playWidget = nullptr;
    for (auto* toolbar : findChildren<QToolBar*>()) {
        if (m_buildAction) buildWidget = toolbar->widgetForAction(m_buildAction);
        if (m_playAction)  playWidget  = toolbar->widgetForAction(m_playAction);
    }

    QRect adjustsMenuRect;
    if (m_adjustsMenu) {
        adjustsMenuRect = menuBar()->actionGeometry(m_adjustsMenu->menuAction());
    }

    QVector<TutorialStep> steps;

    // ── Passo 0: Bem-vindo ────────────────────────────────────────────────────
    steps.append({
        "Bem-vindo ao Tutorial 2!",
        "Neste tutorial vamos montar um detector de proximidade:\n\n"
        "Sensor HC-SR04 + Buzzer — quando algo se aproximar, o buzzer vai bipar!"
        "\n\nSiga os passos e veja a eletrônica ganhando vida.",
        "Clique em 'Próximo' para começar!",
        nullptr, QRect(), TutorialStep::None
    });

    // ── Passo 1: Clicar em Ajustes ───────────────────────────────────────────────
    steps.append({
        "1. Clique no menu 'Ajustes' no topo da tela",
        "Para alterar as configurações de execução do sistema, precisamos abrir o menu Ajustes.\n\n"
        "Clique no menu 'Ajustes' localizado no topo esquerdo da tela.",
        "Clique no menu 'Ajustes'!",
        nullptr, adjustsMenuRect, TutorialStep::Up, true, "", true
    });

    // ── Passo 2: Desativar Multitarefa ─────────────────────────────────────────
    steps.append({
        "2. Desative a opção 'Multitarefa (FreeRTOS)'",
        "Para este tutorial, a simulação do sensor precisa de tempo real contínuo sem divisões de tarefa (multitarefa).\n\n"
        "Desmarque a opção 'Multitarefa (FreeRTOS)' no menu aberto.",
        "Desative a opção 'Multitarefa (FreeRTOS)'!",
        nullptr, QRect(), TutorialStep::Up, true, "", true
    });

    // ── Passo 3: Adicionar HC-SR04 ────────────────────────────────────────────
    steps.append({
        "3. Dê dois cliques no workspace e adicione um HC-SR04",
        "Vamos adicionar o nosso sensor ultrassônico de distância.\n\n"
        "1. Dê DOIS CLIQUES no workspace (mesa de trabalho).\n"
        "2. Digite \"HC-SR04\" ou \"sensor\" na busca e adicione-o.",
        "Dê duplo clique no workspace e adicione o HC-SR04!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 4: VCC → 3V3 ───────────────────────────────────────────────────
    steps.append({
        "4. Conecte o pino VCC do sensor ao pino 3V3 da ESP32",
        "Vamos alimentar o sensor com 3.3V.\n\n"
        "1. Dê um clique no pino VCC do HC-SR04.\n"
        "2. Mova o mouse e clique no pino 3V3 da placa ESP32.",
        "Clique no VCC do HC-SR04 e depois no 3V3 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 5: GND → GND ───────────────────────────────────────────────────
    steps.append({
        "5. Conecte o pino GND do sensor ao pino GND da ESP32",
        "Feche a alimentação do sensor pelo terra.\n\n"
        "1. Dê um clique no pino GND do HC-SR04.\n"
        "2. Mova o mouse e clique em um pino GND da placa ESP32.",
        "Clique no GND do HC-SR04 e depois no GND da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 6: TRIG → GPIO ─────────────────────────────────────────────────
    steps.append({
        "6. Conecte o pino TRIG do sensor ao pino GPIO5 da ESP32",
        "O pino TRIG envia os pulsos que medem a distância.\n\n"
        "1. Dê um clique no pino TRIG do HC-SR04.\n"
        "2. Mova o mouse e clique no pino GPIO5 da placa ESP32.",
        "Conecte o TRIG do HC-SR04 ao pino GPIO5 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 7: ECHO → GPIO ─────────────────────────────────────────────────
    steps.append({
        "7. Conecte o pino ECHO do sensor ao pino GPIO4 da ESP32",
        "O pino ECHO recebe o eco do pulso e calcula a distância.\n\n"
        "1. Dê um clique no pino ECHO do HC-SR04.\n"
        "2. Mova o mouse e clique no pino GPIO4 da placa ESP32.",
        "Conecte o ECHO do HC-SR04 ao pino GPIO4 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 8: Adicionar Buzzer ────────────────────────────────────────────
    steps.append({
        "8. Dê dois cliques no workspace e adicione um Buzzer",
        "O buzzer vai apitar quando algo se aproximar do sensor.\n\n"
        "1. Dê DOIS CLIQUES no workspace.\n"
        "2. Digite \"buzzer\" na busca e adicione-o.",
        "Dê duplo clique no workspace e adicione um Buzzer!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 9: Buzzer + → GPIO ─────────────────────────────────────────────
    steps.append({
        "9. Conecte o pino + (positivo) do Buzzer ao pino GPIO2 da ESP32",
        "O pino positivo do buzzer será controlado pelo microcontrolador.\n\n"
        "1. Dê um clique no pino + do Buzzer.\n"
        "2. Mova o mouse e clique no pino GPIO2 da placa ESP32.",
        "Conecte o pino + do Buzzer ao pino GPIO2 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 10: Buzzer - → GND ───────────────────────────────────────────────
    steps.append({
        "10. Conecte o pino - (negativo) do Buzzer ao GND da ESP32",
        "Feche o circuito do buzzer pelo terra.\n\n"
        "1. Dê um clique no pino - do Buzzer.\n"
        "2. Mova o mouse e clique em um pino GND livre da placa ESP32.",
        "Conecte o - do Buzzer ao GND da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 11: Clique direito → Ao Medir ───────────────────────────────────
    steps.append({
        "11. Clique com botão direito no HC-SR04 e escolha 'Ao Medir'",
        "Vamos abrir o editor de eventos do sensor.\n\n"
        "1. Clique com o BOTÃO DIREITO sobre o HC-SR04 no workspace.\n"
        "2. Selecione o evento \"Ao Medir\" no menu flutuante.",
        "Clique com o botão direito no HC-SR04 e selecione 'Ao Medir'!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 12: Adicionar bloco Ação ──────────────────────────────────────
    steps.append({
        "12. Dê dois cliques no editor e adicione um bloco de Ação",
        "Vamos programar o buzzer para ligar!\n\n"
        "1. Dê DOIS CLIQUES no editor de blocos à direita.\n"
        "2. Adicione uma AÇÃO na lista que aparecer.",
        "Dê duplo clique no editor e adicione um bloco Ação!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // ── Passo 13: Arrastar variável do Buzzer ao campo Alvo ────────────────────
    steps.append({
        "13. Arraste a variável do Buzzer ao campo 'Alvo'",
        "Agora puxe a variável do Buzzer para o bloco de ação.\n\n"
        "1. Na paleta esquerda em 'PINOS E ATUADORES', localize o bloco do Buzzer.\n"
        "2. Clique e segure e arraste até o campo 'Alvo (Pino / Var)' do bloco de Ação.",
        "Arraste a variável do Buzzer para o campo Alvo!",
        nullptr, QRect(), TutorialStep::Right
    });

    // ── Passo 14: Adicionar bloco Aguardar ──────────────────────────
    steps.append({
        "14. Adicione um bloco Aguardar",
        "Um pequeno delay entre o buzz ON e OFF cria o efeito de bip.\n\n"
        "1. Dê DOIS CLIQUES no editor de blocos.\n"
        "2. Adicione um bloco AGUARDAR.",
        "Adicione um bloco Aguardar!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // ── Passo 15: Digitar 50ms ──────────────────────────
    steps.append({
        "15. Digite 50 no campo de milissegundos",
        "Esse será o tempo que o buzzer ficará ligado antes de desligar novamente.\n\n"
        "No campo 'Milissegundos (ms)', digite o número 50.",
        "Digite 50 no campo ms!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // ── Passo 16: Adicionar bloco Ação (buzzer LOW) ──────────────────────
    steps.append({
        "16. Adicione mais um bloco de Ação",
        "Agora vamos adicionar o bloco que vai desligar o buzzer após o bip.\n\n"
        "1. Dê DOIS CLIQUES no editor de blocos.\n"
        "2. Adicione outra AÇÃO.",
        "Dê duplo clique no editor e adicione outro bloco Ação!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // ── Passo 17: Arrastar variável do Buzzer ─────────────
    steps.append({
        "17. Arraste a variável do Buzzer",
        "Vamos configurar o segundo bloco para desligar o buzzer.\n\n"
        "Arraste a variável do Buzzer ao campo 'Alvo' deste novo bloco.",
        "Arraste o Buzzer para o campo Alvo!",
        nullptr, QRect(), TutorialStep::Right
    });

    // ── Passo 18: Mudar para LOW ─────────────
    steps.append({
        "18. Mude o estado para LOW",
        "Agora que o alvo está definido, mude o comando do bloco para desligar o buzzer.\n\n"
        "Clique no campo destacado e mude de HIGH para LOW.",
        "Mude o comando de HIGH para LOW!",
        nullptr, QRect(), TutorialStep::Right, true, "actionCmdCombo"
    });

    // ── Passo 19: Adicionar o bloco Aguardar (delay) ──────────────────────
    steps.append({
        "19. Adicione um bloco Aguardar",
        "Agora vamos adicionar o delay dinâmico que controla o ritmo dos bips!\n\n"
        "1. Dê DOIS CLIQUES no editor de blocos.\n"
        "2. Adicione um bloco AGUARDAR.",
        "Dê duplo clique e adicione um bloco Aguardar!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // ── Passo 20: Arrastar variável distância ao campo ms ──────────────────
    steps.append({
        "20. Arraste a variável distância ao campo de milissegundos",
        "A variável de distância do HC-SR04 vai controlar o intervalo entre os bips.\n\n"
        "1. Em 'VARIÁVEIS GLOBAIS', localize o bloco de distância.\n"
        "2. Arraste-o até o campo 'Milissegundos (ms)' do bloco Aguardar.",
        "Arraste a variável de distância para o campo ms!",
        nullptr, QRect(), TutorialStep::Right
    });

    // ── Passo 21: Adicionar *10 após a variável ─────────────────────────
    steps.append({
        "21. Adicione *10 após a variável no campo ms",
        "Efeito sonar: quanto mais longe o objeto, maior o intervalo entre os bips!\n\n"
        "No campo de milissegundos, clique no final do texto que apareceu\n"
        "e adicione: *10\n\n"
        "Resultado final: \"distancia_2*10\" (ou similar)",
        "Clique no campo ms e adicione *10 no final!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // ── Passo 22: Build ───────────────────────────────────────────────────────
    steps.append({
        "22. Clique no botão de Build para compilar o projeto",
        "Com o circuito montado e a lógica programada, é hora de compilar!\n\n"
        "Clique no botão de Build (ícone de ferramentas na barra superior).",
        "Clique no botão de Build no topo!",
        buildWidget, QRect(), TutorialStep::Up
    });

    // ── Passo 23: Play ────────────────────────────────────────────────────────
    steps.append({
        "23. Clique em Play para iniciar a simulação!",
        "Com o código compilado com sucesso, clique no botão de Play!\n\n"
        "Você vai ver o buzzer bipando em tempo real.",
        "Clique no botão de Play no topo!",
        playWidget, QRect(), TutorialStep::Up
    });

    // ── Passo 24: Observe o buzzer e teste a distância ────────────────────────
    steps.append({
        "24. Clique 2 vezes no sensor e mude a distancia!",
        "O buzzer está bipando em tempo real!\n\n"
        "Dê DOIS CLIQUES no sensor HC-SR04 para abrir as configurações,\n"
        "mude a distância e feche a janela para ver o ritmo do bip mudar.",
        "Dê dois cliques no HC-SR04 e mude a distância simulada!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 25: Abrir o Osciloscópio ──────────────────────────────────────
    steps.append({
        "25. Abra o Osciloscópio",
        "Vamos visualizar os pulsos sonoros em formato de gráfico.\n\n"
        "Clique na aba 'Osciloscópio' localizada na parte inferior esquerda da tela.",
        "Clique na aba Osciloscópio!",
        nullptr, QRect(), TutorialStep::Up, true, "oscilloscopeTab"
    });

    // ── Passo 26: Concluído ─────────────────────────────────────────────────
    steps.append({
        "Parabéns! Seu sonar está concluído!",
        "Você montou e simulou um detector de proximidade ultrassônico do zero!\n\n"
        "Observe no gráfico as ondas de pulso mudando conforme a distância.\n\n"
        "Explore e divirta-se criando novos projetos!",
        "Clique em 'Concluir' para fechar o tutorial.",
        nullptr, QRect(), TutorialStep::None
    });

    m_activeTutorial = 2;
    m_tutorialOverlay->clearVariableDragSteps();
    m_tutorialOverlay->addVariableDragStep(13, "BUZZER");            // drag Buzzer to action target (HIGH)
    m_tutorialOverlay->addVariableDragStep(17, "BUZZER");            // drag Buzzer to action target (LOW)
    m_tutorialOverlay->addVariableDragStep(20, "distancia", "param"); // drag distancia to delay ms field
    m_tutorialOverlay->setSteps(steps);
    m_tutorialOverlay->start();
}

void MainWindow::startMotorIoTTutorial() {
    if (!m_tutorialOverlay) {
        m_tutorialOverlay = new TutorialOverlay(this);
    }

    QWidget* buildWidget = nullptr;
    QWidget* playWidget = nullptr;
    QWidget* webWidget = nullptr;
    for (auto* toolbar : findChildren<QToolBar*>()) {
        if (m_buildAction) buildWidget = toolbar->widgetForAction(m_buildAction);
        if (m_playAction)  playWidget  = toolbar->widgetForAction(m_playAction);
        if (m_webPageAction) webWidget = toolbar->widgetForAction(m_webPageAction);
    }

    QVector<TutorialStep> steps;

    // ── Passo 0: Bem-vindo ────────────────────────────────────────────────────
    steps.append({
        "Bem-vindo ao Tutorial 3!",
        "Neste tutorial vamos criar um Dashboard Web IoT:\n\n"
        "Vamos usar uma página Web (via Wi-Fi) para controlar a posição de um Servomotor!\n\n"
        "Siga os passos e veja a internet das coisas ganhando vida.",
        "Clique em 'Próximo' para começar!",
        nullptr, QRect(), TutorialStep::None
    });

    // ── Passo 1: Adicionar Motor Generico ─────────────────────────────────────────
    steps.append({
        "1. Dê dois cliques no workspace e adicione um Motor Genérico",
        "Vamos adicionar o nosso motor.\n\n"
        "1. Dê DOIS CLIQUES no workspace (mesa de trabalho).\n"
        "2. Digite \"Motor\" na busca e adicione-o.",
        "Dê duplo clique no workspace e adicione um Motor Genérico!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 2: GND → GND ───────────────────────────────────────────────────
    steps.append({
        "2. Conecte o pino GND do motor ao pino GND da ESP32",
        "O pino GND (geralmente marrom ou preto) vai para o terra.\n\n"
        "1. Dê um clique no pino GND do Motor Genérico.\n"
        "2. Mova o mouse e clique em um pino GND da placa ESP32.",
        "Clique no GND do Motor e depois no GND da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 3: VCC → 5V ou 3V3 ─────────────────────────────────────────────
    steps.append({
        "3. Conecte o pino VCC do motor a um pino de alimentação da ESP32",
        "O pino VCC (vermelho) alimenta o motor.\n\n"
        "1. Dê um clique no pino VCC do Motor Genérico.\n"
        "2. Mova o mouse e clique no pino 5V ou 3V3 da placa ESP32.",
        "Clique no VCC do Motor e depois no 5V/3V3 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 4: PWM → GPIO ──────────────────────────────────────────────────
    steps.append({
        "4. Conecte o pino PWM (Sinal) ao pino GPIO2 da ESP32",
        "O pino PWM (laranja) envia o ângulo desejado para o motor.\n\n"
        "1. Dê um clique no pino PWM do Motor Genérico.\n"
        "2. Mova o mouse e clique no pino GPIO2 da placa ESP32.",
        "Conecte o PWM do Motor ao pino GPIO2 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // ── Passo 5: Dashboard Web ───────────────────────────────────────────────
    steps.append({
        "5. Clique no ícone de Construtor Web",
        "Agora vamos desenhar a página da web!\n\n"
        "Clique no botão 'Dashboard Web' na barra de ferramentas.",
        "Clique no botão Dashboard Web na barra superior!",
        webWidget, QRect(), TutorialStep::Up, true
    });

    // ── Passo 6: Duplo Clique e Slider ───────────────────────────────────────
    steps.append({
        "6. Dê dois cliques na tela e adicione um Slider",
        "A janela do Construtor Web abriu.\n\n"
        "1. Dê DOIS CLIQUES na área branca do Dashboard Web.\n"
        "2. Digite \"Slider\" e adicione-o.",
        "Dê duplo clique e adicione um Slider na tela web!",
        nullptr, QRect(), TutorialStep::None, false, "webScene"
    });

    // ── Passo 7: Botão Direito -> Ao Alterar Valor ───────────────────────────
    steps.append({
        "7. Clique direito no Slider e edite o evento",
        "Precisamos programar o que acontece quando o slider for arrastado pelo usuário.\n\n"
        "1. Clique com o BOTÃO DIREITO sobre o Slider que você acabou de adicionar.\n"
        "2. Selecione 'Editar Evento: Ao Alterar Valor'.",
        "Clique direito no Slider e selecione 'Ao Alterar Valor'!",
        nullptr, QRect(), TutorialStep::None, false, "webScene"
    });

    // ── Passo 8: Adicionar Girar Motor ───────────────────────────────────────
    steps.append({
        "8. Dê dois cliques e adicione 'Girar Motor'",
        "No editor de blocos que se abriu, vamos mandar o motor girar.\n\n"
        "1. Dê DOIS CLIQUES na área cinza.\n"
        "2. Procure por 'Girar Motor' e adicione-o.",
        "Dê duplo clique e adicione um bloco Girar Motor!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // ── Passo 9: Arrastar variável Motor ─────────────────────────────────────
    steps.append({
        "9. Arraste a variável do Motor para o campo Alvo",
        "Indique qual motor vai girar.\n\n"
        "Na lista 'PINOS E ATUADORES' à esquerda, arraste a variável do Motor para o campo 'Alvo'.",
        "Arraste a variável do Motor para o Alvo!",
        nullptr, QRect(), TutorialStep::Right
    });

    // ── Passo 10: Arrastar variável Slider ───────────────────────────────────
    steps.append({
        "10. Arraste a variável da porcentagem do slider para o campo Ângulo",
        "A variável 'webslider_1' (ou similar) contém a porcentagem atual do slider na página (0 a 100).\n\n"
        "Na lista 'VARIÁVEIS DO EVENTO' à esquerda, arraste a variável da porcentagem do slider para o campo 'Ângulo'.",
        "Arraste a variável da porcentagem do slider para o Ângulo!",
        nullptr, QRect(), TutorialStep::Right
    });

    // ── Passo 11: Fechar o Editor de Blocos ──────────────────────────────────
    steps.append({
        "11. Saia do Editor de Eventos",
        "Nosso código está pronto.\n\n"
        "Clique fora do editor de eventos (na área cinza ou em outro componente) para fechá-lo.",
        "Clique fora do evento para fechar!",
        nullptr, QRect(), TutorialStep::Up, false, "workspaceScene"
    });

    // ── Passo 12: Abrir Web View novamente ───────────────────────────────────
    steps.append({
        "12. Clique no ícone Construtor Web novamente",
        "Volte para a janela de edição do Dashboard.",
        "Clique no botão Dashboard Web na barra superior!",
        webWidget, QRect(), TutorialStep::Up, true
    });

    // ── Passo 13: Habilitar WebPage ──────────────────────────────────────────
    steps.append({
        "13. Marque a caixa 'Habilitar WebPage'",
        "Para que o ESP32 ative a página web no Wi-Fi, precisamos marcar a caixa superior esquerda.",
        "Marque a caixa 'Habilitar WebPage'!",
        nullptr, QRect(), TutorialStep::Up, false, "webEnableSwitch"
    });

    // ── Passo 14: Fechar Web View ───────────────────────────────────
    steps.append({
        "14. Feche a janela",
        "Sua página web está pronta e ativada!",
        "Clique no 'X' na janela do Web View para fechar!",
        nullptr, QRect(), TutorialStep::Down, false, ""
    });

    // ── Passo 15: Compilar Projeto ───────────────────────────────────────────
    steps.append({
        "15. Clique no botão de Build para compilar",
        "Tudo configurado! Agora clique no martelo para compilar o código gerado em C++.",
        "Clique no botão Compilar (martelo)!",
        buildWidget, QRect(), TutorialStep::Up, true
    });

    // ── Passo 16: Play! ──────────────────────────────────────────────────────
    steps.append({
        "16. Inicie a simulação!",
        "Chegou a hora mágica.\n\n"
        "1. Clique no botão Play.\n"
        "2. Espere o terminal mostrar que o Wi-Fi está conectado.\n"
        "3. Abra o site ou visualize o slider no simulador para controlar o seu motor SG90 pela web!",
        "Clique no botão Play!",
        playWidget, QRect(), TutorialStep::Up, true
    });

    steps.append({
        "Parabéns! Você concluiu o Tutorial 3! 🎉",
        "Incrível! Você acabou de criar um sistema IoT de verdade, onde um dashboard web envia sinais Wi-Fi para movimentar um servomotor remotamente.\n\n"
        "Explore a IDE, mude o tipo de pino, ou crie seus próprios projetos do zero!",
        "Tutorial concluído com sucesso!",
        nullptr, QRect(), TutorialStep::None
    });

    m_activeTutorial = 3;
    m_tutorialOverlay->clearVariableDragSteps();
    m_tutorialOverlay->addVariableDragStep(9, "MOTOR"); // drag motor to servo block target
    m_tutorialOverlay->addVariableDragStep(10, "webslider_1", "param"); // drag webslider to servo block param
    m_tutorialOverlay->setSteps(steps);
    m_tutorialOverlay->start();
}

void MainWindow::startLdrRelayTutorial() {
    if (!m_tutorialOverlay) {
        m_tutorialOverlay = new TutorialOverlay(this);
    }

    QWidget* buildWidget = nullptr;
    QWidget* playWidget = nullptr;
    for (auto* toolbar : findChildren<QToolBar*>()) {
        if (m_buildAction) buildWidget = toolbar->widgetForAction(m_buildAction);
        if (m_playAction)  playWidget  = toolbar->widgetForAction(m_playAction);
    }

    QVector<TutorialStep> steps;

    // Passo 0: Bem-vindo
    steps.append({
        "Bem-vindo ao Tutorial 4!",
        "Neste tutorial vamos criar um circuito de Luz Automática:\n\n"
        "Usaremos um sensor de luz LDR para ligar uma lâmpada através de um módulo de Relé quando o ambiente estiver escuro!"
        "\n\nSiga os passos e veja a lógica de controle analógico em funcionamento.",
        "Clique em 'Próximo' para começar!",
        nullptr, QRect(), TutorialStep::None
    });

    // Passo 1: Adicionar LDR
    steps.append({
        "1. Dê dois cliques no workspace e adicione um LDR",
        "Vamos começar adicionando o sensor de luminosidade (LDR).\n\n"
        "1. Dê DOIS CLIQUES no workspace (mesa de trabalho).\n"
        "2. Digite \"LDR\" na busca e adicione o componente.",
        "Dê duplo clique no workspace e adicione o LDR!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 2: Adicionar Resistor
    steps.append({
        "2. Dê dois cliques no workspace e adicione um Resistor",
        "Como este é um sensor LDR de 2 pinos, precisamos criar um divisor de tensão usando um resistor.\n\n"
        "1. Dê DOIS CLIQUES no workspace.\n"
        "2. Digite \"Resistor\" na busca e adicione o componente.",
        "Dê duplo clique no workspace e adicione um Resistor!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 3: Editar Resistência do Resistor
    steps.append({
        "3. Dê dois cliques no Resistor e mude seu valor para 10 kΩ",
        "Para que o divisor de tensão com o LDR funcione corretamente, use um resistor de 10 kΩ (10000 Ω).\n\n"
        "1. Dê DOIS CLIQUES no Resistor no workspace para abrir a janela de edição.\n"
        "2. No campo de valor da resistência, digite 10000 ou clique no atalho rápido '10 kΩ'.\n"
        "3. Clique em 'Salvar'.",
        "Dê dois cliques no Resistor e configure seu valor para 10 kΩ!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 4: LDR Pin 1 -> ESP32 3V3
    steps.append({
        "4. Conecte o pino 1 do LDR ao pino 3V3 da ESP32",
        "Alimente um dos lados do sensor LDR com 3.3V.\n\n"
        "1. Dê um clique no pino 1 do LDR.\n"
        "2. Mova o mouse e clique no pino 3V3 da ESP32.",
        "Conecte o pino 1 do LDR ao pino 3V3 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 5: LDR Pin 2 -> ESP32 GPIO3
    steps.append({
        "5. Conecte o pino 2 do LDR ao pino GPIO3 da ESP32",
        "Ligue o sinal analógico do sensor LDR ao pino de leitura analógica.\n\n"
        "1. Dê um clique no pino 2 do LDR.\n"
        "2. Mova o mouse e clique no pino GPIO3 da ESP32.",
        "Conecte o pino 2 do LDR ao pino GPIO3 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 6: Resistor Pin 1 -> LDR Pin 2
    steps.append({
        "6. Conecte o pino 1 do Resistor ao pino 2 do LDR",
        "Agora conecte um dos lados do resistor de pull-down ao mesmo nó de sinal do LDR.\n\n"
        "1. Dê um clique no pino 1 do Resistor.\n"
        "2. Mova o mouse e clique no pino 2 do LDR.",
        "Conecte o pino 1 do Resistor ao pino 2 do LDR!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 7: Resistor Pin 2 -> ESP32 GND
    steps.append({
        "7. Conecte o pino 2 do Resistor a um pino GND da ESP32",
        "Aterre o outro lado do divisor de tensão para completar o circuito.\n\n"
        "1. Dê um clique no pino 2 do Resistor.\n"
        "2. Mova o mouse e clique em um pino GND da ESP32.",
        "Conecte o pino 2 do Resistor ao pino GND da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 8: Adicionar Relé
    steps.append({
        "8. Dê dois cliques no workspace e adicione um Relé",
        "O módulo de relé permite controlar a lâmpada com segurança.\n\n"
        "1. Dê DOIS CLIQUES no workspace.\n"
        "2. Digite \"relé\" ou \"rele\" na busca e adicione-o.",
        "Dê duplo clique no workspace e adicione um Relé!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 9: Relé VCC -> 5V ou 3V3
    steps.append({
        "9. Conecte o pino VCC do Relé ao pino 5V ou 3V3 da ESP32",
        "Alimente o módulo de relé.\n\n"
        "1. Dê um clique no pino VCC do Relé.\n"
        "2. Mova o mouse e clique no pino 5V ou 3V3 da placa ESP32.",
        "Clique no VCC do Relé e depois no 5V ou 3V3 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 10: Relé GND -> GND
    steps.append({
        "10. Conecte o pino GND do Relé ao pino GND da ESP32",
        "Feche o circuito de alimentação do Relé.\n\n"
        "1. Dê um clique no pino GND do Relé.\n"
        "2. Mova o mouse e clique em um pino GND livre da placa ESP32.",
        "Conecte o GND do Relé ao pino GND da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 11: Relé IN -> GPIO2
    steps.append({
        "11. Conecte o pino IN do Relé ao pino GPIO2 da ESP32",
        "O pino IN receberá o sinal de controle (HIGH/LOW) do microcontrolador.\n\n"
        "1. Dê um clique no pino IN (Sinal) do Relé.\n"
        "2. Mova o mouse e clique no pino GPIO2 da ESP32.",
        "Conecte o IN do Relé ao pino GPIO2 da ESP32!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 12: Adicionar Lâmpada
    steps.append({
        "12. Dê dois cliques no workspace e adicione uma Lâmpada",
        "Adicione a lâmpada que será controlada pelo relé.\n\n"
        "1. Dê DOIS CLIQUES no workspace.\n"
        "2. Digite \"lâmpada\" ou \"lampada\" na busca e adicione-a.",
        "Dê duplo clique no workspace e adicione a Lâmpada!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 13: Conectar Relé NO -> Lâmpada FASE
    steps.append({
        "13. Conecte o pino NO do Relé ao pino FASE da Lâmpada",
        "Conecte a saída normalmente aberta (NO) do relé à lâmpada para acioná-la.\n\n"
        "1. Dê um clique no pino NO do Relé.\n"
        "2. Mova o mouse e clique no pino FASE da Lâmpada.",
        "Conecte o NO do Relé ao pino FASE da Lâmpada!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 14: Right click LDR -> Ao Alterar
    steps.append({
        "14. Clique com botão direito no LDR e escolha 'Ao Alterar'",
        "Vamos abrir a área de programação para dar comportamento ao circuito.\n\n"
        "1. Clique com o BOTÃO DIREITO sobre o LDR no workspace.\n"
        "2. Selecione o evento \"Ao Alterar\" no menu flutuante.",
        "Clique com o botão direito no LDR e selecione 'Ao Alterar'!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 15: Adicionar bloco Condicional (SE)
    steps.append({
        "15. Dê dois cliques no editor de blocos e adicione um Condicional (SE)",
        "Vamos criar uma regra baseada na intensidade de luz.\n\n"
        "1. Dê DOIS CLIQUES na área cinza do editor de blocos à direita.\n"
        "2. Digite \"se\" ou \"condicional\" e adicione o bloco.",
        "Dê duplo clique e adicione o bloco Condicional (SE)!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // Passo 16: Digitar luminosidade < 10
    steps.append({
        "16. Digite a condição 'luminosidade < 10' na expressão condicional",
        "Se a luminosidade lida pelo LDR (0-100%) for menor que 10%, significa que o ambiente está escuro.\n\n"
        "No campo de texto do bloco de condição, digite exatamente:\n"
        "luminosidade < 10\n\n"
        "(Nota: O parâmetro 'luminosidade' é injetado pelo evento e representa a intensidade de luz de 0 a 100%).",
        "Digite 'luminosidade < 10' na Expressão Condicional!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // Passo 17: Adicionar bloco Ação
    steps.append({
        "17. Dê dois cliques no editor e adicione um bloco de Ação",
        "Agora vamos programar o relé para ligar a lâmpada.\n\n"
        "1. Dê DOIS CLIQUES no editor de blocos.\n"
        "2. Adicione uma AÇÃO na lista.",
        "Dê duplo clique e adicione um bloco de Ação!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // Passo 18: Arrastar variável do Relé para o Alvo
    steps.append({
        "18. Arraste a variável RELE_1 da paleta ao campo Alvo da Ação",
        "Configure o relé como alvo deste acionamento.\n\n"
        "1. Na paleta à esquerda em 'PINOS E ATUADORES', clique e segure o bloco rosa 'RELE_1 [GPIO2]'.\n"
        "2. Arraste e solte-o no campo 'Alvo' do bloco de Ação.",
        "Arraste a variável RELE_1 para o campo Alvo!",
        nullptr, QRect(), TutorialStep::Right
    });

    // Passo 19: Arrastar Ação para dentro do Condicional
    steps.append({
        "19. Arraste o bloco de Ação para dentro do bloco Condicional (SE)",
        "Agora conecte a Ação ao fluxo da decisão condicional.\n\n"
        "Clique e segure na barra superior do bloco de Ação e arraste-o para cima, encaixando-o logo abaixo da linha do Condicional (SE) até que ele fique recuado para a direita.",
        "Arraste a Ação para dentro do bloco Condicional (SE)!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // Passo 20: Adicionar bloco Senão (else)
    steps.append({
        "20. Dê dois cliques no editor e adicione um bloco Senão (else)",
        "Agora criaremos o caso alternativo (quando o ambiente estiver claro).\n\n"
        "1. Dê DOIS CLIQUES no editor de blocos.\n"
        "2. Digite \"senão\" ou \"else\" e adicione o bloco.",
        "Dê duplo clique e adicione o bloco Senão!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // Passo 21: Adicionar bloco Ação (LOW)
    steps.append({
        "21. Dê dois cliques no editor e adicione mais um bloco de Ação",
        "Esta nova ação será usada para desligar o relé.\n\n"
        "1. Dê DOIS CLIQUES no editor de blocos.\n"
        "2. Adicione outra AÇÃO.",
        "Dê duplo clique e adicione outro bloco Ação!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // Passo 22: Arrastar variável do Relé para a segunda Ação
    steps.append({
        "22. Arraste a variável RELE_1 ao campo 'Alvo' desta nova Ação",
        "Indique que essa segunda ação também controla o mesmo relé.\n\n"
        "Arraste a variável 'RELE_1' da paleta esquerda ao campo 'Alvo' do novo bloco.",
        "Arraste a variável RELE_1 para o campo Alvo!",
        nullptr, QRect(), TutorialStep::Right
    });

    // Passo 23: Mudar comando para LOW
    steps.append({
        "23. Mude o estado do bloco de Ação para LOW",
        "Como este bloco trata o ambiente claro, mude o comando para desligar a lâmpada.\n\n"
        "Clique no campo destacado da segunda Ação e mude de HIGH para LOW.",
        "Mude o comando de HIGH para LOW!",
        nullptr, QRect(), TutorialStep::Right, true, "actionCmdCombo"
    });

    // Passo 24: Arrastar segunda Ação para dentro do Senão (else)
    steps.append({
        "24. Arraste a segunda Ação para dentro do bloco Senão (else)",
        "Agora conecte esta ação de desligar ao bloco alternativo.\n\n"
        "Clique e arraste o segundo bloco de Ação para cima, encaixando-o logo abaixo da linha do bloco Senão (else) até ficar recuado.",
        "Arraste a segunda Ação para dentro do bloco Senão!",
        m_blockEditor, QRect(), TutorialStep::Right
    });

    // Passo 25: Build
    steps.append({
        "25. Clique no botão de Build no topo para compilar",
        "Com o circuito e a lógica prontos, clique no ícone de ferramentas na barra superior para compilar o código.",
        "Clique no botão de Build no topo para compilar!",
        buildWidget, QRect(), TutorialStep::Up
    });

    // Passo 26: Play
    steps.append({
        "26. Clique em Play para iniciar a simulação",
        "Com a compilação finalizada, inicie a simulação em tempo real!",
        "Clique no botão de Play no topo para iniciar a simulação!",
        playWidget, QRect(), TutorialStep::Up
    });

    // Passo 27: Dê dois cliques no LDR
    steps.append({
        "27. Dê dois cliques no LDR e mude a luminosidade",
        "O simulador está rodando!\n\n"
        "1. Dê DOIS CLIQUES no LDR no workspace para abrir a tela de propriedades.\n"
        "2. Arraste a barra de luminosidade para baixo de 1500 (ambiente escuro) para ver o relé ativar (a lâmpada acenderá), e para cima de 1500 para desligar.",
        "Dê dois cliques no LDR e altere a luminosidade simulada!",
        m_view, QRect(), TutorialStep::Up, false
    });

    // Passo 28: Done
    steps.append({
        "Parabéns! Sua Luz Automática está concluída!",
        "Você acabou de criar um circuito inteligente de controle de iluminação usando LDR, Relé e Lâmpada!\n\n"
        "Experimente alterar a luz do sensor e veja a lâmpada acender e apagar dinamicamente.\n\n"
        "Divirta-se criando novos projetos!",
        "Clique em 'Concluir' para fechar o tutorial.",
        nullptr, QRect(), TutorialStep::None
    });

    m_activeTutorial = 4;
    m_tutorialOverlay->clearVariableDragSteps();
    m_tutorialOverlay->addVariableDragStep(18, "RELE"); // drag RELE to action target (HIGH)
    m_tutorialOverlay->addVariableDragStep(22, "RELE"); // drag RELE to action target (LOW)
    m_tutorialOverlay->setSteps(steps);
    m_tutorialOverlay->start();
}


void MainWindow::checkBlockEditorTutorialSteps() {
    if (!m_tutorialOverlay || !m_tutorialOverlay->isVisible()) return;

    bool advanced = true;
    while (advanced) {
        advanced = false;
        int step = m_tutorialOverlay->currentStep();

        QVector<EventLogicBlock> active = m_blockEditor->getActiveBlocks();
        int  actionCount     = 0;   // non-DELAY action blocks
        int  delayCount      = 0;   // DELAY blocks
        int  filledCount     = 0;   // action blocks with target filled
        int  delayParamCount = 0;   // DELAY blocks with param filled
        int  delayMultCount  = 0;   // DELAY blocks with *10 in param

        for (const auto& b : active) {
            if (b.type == LogicBlockType::ACTION) {
                if (b.actionCommand == "DELAY") {
                    delayCount++;
                    if (!b.actionParam.trimmed().isEmpty()) delayParamCount++;
                    if (b.actionParam.contains("*10")) delayMultCount++;
                } else {
                    actionCount++;
                    if (!b.actionTarget.trimmed().isEmpty()) filledCount++;
                }
            }
        }

        if (m_activeTutorial == 1) {
            // Tutorial 1 — LED/Button
            // Step 11: user added an action block (target may be empty)
            if (step == 11 && actionCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 12: user dragged variable to target field
            else if (step == 12 && filledCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }

        } else if (m_activeTutorial == 2) {
            // Tutorial 2 — Distance Sensor
            // Step 12: add first action block (empty target OK)
            if (step == 12 && actionCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 13: drag buzzer variable to that action's target
            else if (step == 13 && filledCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 14: add a DELAY block (empty OK)
            else if (step == 14 && delayCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 15: type 50 in first DELAY block
            else if (step == 15 && delayParamCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 16: add second action block (empty target OK)
            else if (step == 16 && actionCount >= 2) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 17: drag buzzer variable to second action (target filled)
            else if (step == 17 && filledCount >= 2) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 18: user changes combo to LOW
            else if (step == 18) {
                // To check if they set it to LOW, we must verify the last action block
                int lowCount = 0;
                for (const auto& b : active) {
                    if (b.type == LogicBlockType::ACTION && b.actionCommand == "LOW") {
                        lowCount++;
                    }
                }
                if (lowCount >= 1) {
                    m_tutorialOverlay->advance(); advanced = true;
                }
            }
            // Step 19: add second DELAY block (empty param OK)
            else if (step == 19 && delayCount >= 2) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 20: drag distance var to delay param
            else if (step == 20 && delayParamCount >= 2) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 21: add *10 to param
            else if (step == 21 && delayMultCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }

        } else if (m_activeTutorial == 3) {
            // Tutorial 3 — Motor & IoT
            
            int rotateMotorCount = 0;
            int rotateTargetFilled = 0;
            int rotateParamFilled = 0;
            
            for (const auto& b : active) {
                if (b.type == LogicBlockType::ACTION && b.actionCommand == "ROTATE_MOTOR") {
                    rotateMotorCount++;
                    if (!b.actionTarget.trimmed().isEmpty()) rotateTargetFilled++;
                    if (!b.actionParam.trimmed().isEmpty()) rotateParamFilled++;
                }
            }
            
            // Step 8: Add 'Girar Motor' block
            if (step == 8 && rotateMotorCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 9: Drag motor variable to target
            else if (step == 9 && rotateTargetFilled >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 10: Drag 'valor' variable to param (angle)
            else if (step == 10 && rotateParamFilled >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
        } else if (m_activeTutorial == 4) {
            // Tutorial 4 — LDR & Relay
            int condCount = 0;
            int condFilled = 0;
            int elseCount = 0;
            int lowCount = 0;
            for (const auto& b : active) {
                if (b.type == LogicBlockType::CONDITION) {
                    if (b.conditionExpression == "senao" || b.conditionExpression == "else") {
                        elseCount++;
                    } else {
                        condCount++;
                        if (b.conditionExpression.contains("luminosidade < 10") || b.conditionExpression.contains("luminosidade <10") || b.conditionExpression.contains("luminosidade<10") ||
                            b.conditionExpression.contains("luminosidade < 1500") || b.conditionExpression.contains("luminosidade <1500") || b.conditionExpression.contains("luminosidade<1500") ||
                            b.conditionExpression.contains("valor < 1500") || b.conditionExpression.contains("valor <1500") || b.conditionExpression.contains("valor<1500")) {
                            condFilled++;
                        }
                    }
                } else if (b.type == LogicBlockType::ACTION && b.actionCommand == "LOW") {
                    lowCount++;
                }
            }

            // Step 15: Add CONDITION block
            if (step == 15 && condCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 16: Type condition (valor < 1500)
            else if (step == 16 && condFilled >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 17: Add ACTION block
            else if (step == 17 && actionCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 18: Drag RELE_1 to action target
            else if (step == 18 && filledCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 19: Drag first Action inside Condicional (SE)
            else if (step == 19) {
                bool actionInsideCond = false;
                int condIndex = -1;
                int fimIndex = -1;
                for (int i = 0; i < active.size(); ++i) {
                    if (active[i].type == LogicBlockType::CONDITION && 
                        !(active[i].conditionExpression == "senao" || active[i].conditionExpression == "else")) {
                        condIndex = i;
                    }
                    if (condIndex != -1 && active[i].type == LogicBlockType::FIM) {
                        fimIndex = i;
                        break;
                    }
                }
                if (condIndex != -1 && fimIndex != -1) {
                    for (int i = condIndex + 1; i < fimIndex; ++i) {
                        if (active[i].type == LogicBlockType::ACTION && active[i].actionTarget.contains("RELE")) {
                            actionInsideCond = true;
                            break;
                        }
                    }
                }
                if (actionInsideCond) {
                    m_tutorialOverlay->advance(); advanced = true;
                }
            }
            // Step 20: Add SENÃO block
            else if (step == 20 && elseCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 21: Add second ACTION block
            else if (step == 21 && actionCount >= 2) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 22: Drag RELE_1 to second action target
            else if (step == 22 && filledCount >= 2) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 23: Change state to LOW
            else if (step == 23 && lowCount >= 1) {
                m_tutorialOverlay->advance(); advanced = true;
            }
            // Step 24: Drag second Action inside Senão (else)
            else if (step == 24) {
                bool actionInsideElse = false;
                int elseIndex = -1;
                int fimIndex = -1;
                for (int i = 0; i < active.size(); ++i) {
                    if (active[i].type == LogicBlockType::CONDITION && 
                        (active[i].conditionExpression == "senao" || active[i].conditionExpression == "else")) {
                        elseIndex = i;
                    }
                    if (elseIndex != -1 && active[i].type == LogicBlockType::FIM) {
                        fimIndex = i;
                        break;
                    }
                }
                if (elseIndex != -1 && fimIndex != -1) {
                    for (int i = elseIndex + 1; i < fimIndex; ++i) {
                        if (active[i].type == LogicBlockType::ACTION && 
                            active[i].actionTarget.contains("RELE") && 
                            active[i].actionCommand == "LOW") {
                            actionInsideElse = true;
                            break;
                        }
                    }
                }
                if (actionInsideElse) {
                    m_tutorialOverlay->advance(); advanced = true;
                }
            }
        }

    }
}


void MainWindow::synchronizeLoopBlocks() {
    // 1. Get the ESP32 component if it exists.
    ComponentItem* esp32 = nullptr;
    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "esp32") {
            esp32 = comp;
            break;
        }
    }
    if (!esp32) return;

    QString loopKey = QString("%1:aoLoop").arg(esp32->id());

    // 2. Fetch the current blocks for `esp32:aoLoop` from the storage.
    QMap<QString, QVector<EventLogicBlock>> storage = m_blockEditor->getEventBlockStorage();
    QVector<EventLogicBlock> loopBlocks = storage.value(loopKey);

    // 3. Collect all native monitors and custom component loop functions that should run.
    QStringList expectedCalls;

    for (auto* comp : m_scene->components()) {
        if (comp->componentType() == "button") {
            QString name = sanitizeIdentifier(comp->name());
            
            QString eventKey = QString("%1:aoClicar").arg(comp->id());
            if (storage.contains(eventKey) && !storage[eventKey].isEmpty()) {
                expectedCalls.append(QString("monitor_%1_eventAoClicar()").arg(name));
            }
            QString pressKey = QString("%1:aoPressionar").arg(comp->id());
            if (storage.contains(pressKey) && !storage[pressKey].isEmpty()) {
                expectedCalls.append(QString("monitor_%1_eventAoPressionar()").arg(name));
            }
            QString releaseKey = QString("%1:aoSoltar").arg(comp->id());
            if (storage.contains(releaseKey) && !storage[releaseKey].isEmpty()) {
                expectedCalls.append(QString("monitor_%1_eventAoSoltar()").arg(name));
            }
        } else if (comp->componentType() == "led" || comp->componentType() == "rgb_led") {
            QString eventKey = QString("%1:aoLigar").arg(comp->id());
            if (storage.contains(eventKey) && !storage[eventKey].isEmpty()) {
                QString name = sanitizeIdentifier(comp->name());
                expectedCalls.append(QString("monitor_%1_eventAoLigar()").arg(name));
            }
        } else if (comp->componentType() == "potentiometer") {
            QString eventKey = QString("%1:aoGirar").arg(comp->id());
            if (storage.contains(eventKey) && !storage[eventKey].isEmpty()) {
                QString name = sanitizeIdentifier(comp->name());
                expectedCalls.append(QString("monitor_%1_eventAoGirar()").arg(name));
            }
        } else if (comp->componentType() == "buzzer") {
            QString eventKey = QString("%1:aoTocar").arg(comp->id());
            if (storage.contains(eventKey) && !storage[eventKey].isEmpty()) {
                QString name = sanitizeIdentifier(comp->name());
                expectedCalls.append(QString("monitor_%1_eventAoTocar()").arg(name));
            }
        } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
            QString category = custom->category();
            if (category == "digital_trigger") {
                QString eventKey = QString("%1:aoClicar").arg(custom->id());
                if (storage.contains(eventKey) && !storage[eventKey].isEmpty()) {
                    QString name = sanitizeIdentifier(custom->name());
                    expectedCalls.append(QString("monitor_%1_eventAoClicar()").arg(name));
                }
            } else if (category == "digital_actuator") {
                QString eventKey = QString("%1:aoLigar").arg(custom->id());
                if (storage.contains(eventKey) && !storage[eventKey].isEmpty()) {
                    QString name = sanitizeIdentifier(custom->name());
                    expectedCalls.append(QString("monitor_%1_eventAoLigar()").arg(name));
                }
            } else if (category == "analog_input") {
                QString eventKey = QString("%1:aoGirar").arg(custom->id());
                if (storage.contains(eventKey) && !storage[eventKey].isEmpty()) {
                    QString name = sanitizeIdentifier(custom->name());
                    expectedCalls.append(QString("monitor_%1_eventAoGirar()").arg(name));
                }
            } else if (category == "active_actuator") {
                QString eventKey = QString("%1:aoTocar").arg(custom->id());
                if (storage.contains(eventKey) && !storage[eventKey].isEmpty()) {
                    QString name = sanitizeIdentifier(custom->name());
                    expectedCalls.append(QString("monitor_%1_eventAoTocar()").arg(name));
                }
            }

            // Custom functions containing "loop" (case-insensitive)
            QString compName = sanitizeIdentifier(custom->name());
            for (const auto& fn : custom->definition().customFunctions) {
                if (fn.name.contains("loop", Qt::CaseInsensitive)) {
                    expectedCalls.append(QString("%1_%2()").arg(compName).arg(fn.name));
                }
            }
        }
    }

    // 4. Update loopBlocks, pruning dead calls and preserving others
    QVector<EventLogicBlock> updatedBlocks;
    QStringList existingCalls;

    for (const auto& block : loopBlocks) {
        if (block.type == LogicBlockType::ACTION && block.actionCommand == "CALL_FUNCTION") {
            QString target = block.actionTarget.trimmed();
            if (!target.endsWith("()")) {
                target += "()";
            }

            bool isMonitor = target.startsWith("monitor_") && (
                target.endsWith("_eventAoClicar()") || 
                target.endsWith("_eventAoPressionar()") || 
                target.endsWith("_eventAoSoltar()") || 
                target.endsWith("_eventAoLigar()") || 
                target.endsWith("_eventAoGirar()") || 
                target.endsWith("_eventAoTocar()")
            );
            bool isCustomLoop = false;
            
            for (auto* comp : m_scene->components()) {
                if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
                    QString compName = sanitizeIdentifier(custom->name());
                    if (target.startsWith(compName + "_")) {
                        for (const auto& fn : custom->definition().customFunctions) {
                            if (fn.name.contains("loop", Qt::CaseInsensitive)) {
                                if (target == QString("%1_%2()").arg(compName).arg(fn.name)) {
                                    isCustomLoop = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (isMonitor || isCustomLoop) {
                if (expectedCalls.contains(target)) {
                    updatedBlocks.append(block);
                    existingCalls.append(target);
                }
            } else {
                updatedBlocks.append(block);
            }
        } else {
            updatedBlocks.append(block);
        }
    }

    // Append newly expected calls that aren't already present
    static int syncCounter = 1000;
    for (const auto& expected : expectedCalls) {
        if (!existingCalls.contains(expected)) {
            EventLogicBlock b;
            b.id = QString("sync_action_%1").arg(syncCounter++);
            b.type = LogicBlockType::ACTION;
            b.actionCommand = "CALL_FUNCTION";
            b.actionTarget = expected;
            updatedBlocks.append(b);
        }
    }

    // 5. Update block editor
    m_blockEditor->setEventBlocks(esp32->id(), "aoLoop", updatedBlocks);
}







void MainWindow::showComponentModeling(ComponentItem* comp) {
    if (!comp) return;

    QDialog dialog(this);
    dialog.setWindowTitle(QString("Modelagem Eletrofísica — %1").arg(comp->name()));
    dialog.setMinimumSize(880, 720);
    dialog.setStyleSheet(
        "QDialog { background-color: #F8FAFC; border: 1px solid #E2E8F0; }"
        "QLabel#dialogTitle { color: #0F172A; font-family: 'Segoe UI', -apple-system, sans-serif; font-size: 20px; font-weight: 700; }"
        "QTextBrowser { background-color: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 8px; padding: 20px; font-family: 'Segoe UI', -apple-system, sans-serif; font-size: 13px; line-height: 1.6; color: #334155; }"
        "QPushButton { background: #2563EB; border: none; border-radius: 6px; color: white; padding: 10px 24px; font-weight: 600; font-size: 13px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #1D4ED8; }"
        "QPushButton:pressed { background: #1E40AF; }"
    );

    auto* mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    // Cabeçalho
    auto* headerLayout = new QHBoxLayout();
    auto* titleLabel = new QLabel(QString("Referência de Modelagem: %1 (%2)").arg(comp->name()).arg(comp->id()), &dialog);
    titleLabel->setObjectName("dialogTitle");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Descrição Eletrofísica (Texto & Tabelas)
    auto* browser = new QTextBrowser(&dialog);
    browser->setOpenExternalLinks(true);

    QString html;
    QString code;
    QString type = comp->componentType();

    // Template base CSS compatível com QTextBrowser
    QString css = 
        "<style>"
        "body { font-family: 'Segoe UI', Arial, sans-serif; color: #334155; margin: 0; padding: 0; background: #FFFFFF; }"
        "h1 { color: #0F172A; font-size: 18px; font-weight: 800; margin-top: 0; margin-bottom: 6px; padding-bottom: 6px; border-bottom: 1px solid #E2E8F0; }"
        "h2 { color: #1E293B; font-size: 14px; font-weight: 700; margin-top: 16px; margin-bottom: 6px; }"
        "p { margin: 0 0 8px 0; line-height: 1.5; font-size: 12px; }"
        "ul { margin: 0 0 10px 0; padding-left: 20px; }"
        "li { margin-bottom: 4px; font-size: 12px; }"
        ".badge { background-color: #EFF6FF; color: #1D4ED8; font-size: 10px; font-weight: 700; padding: 3px 8px; border-radius: 4px; display: inline; text-transform: uppercase; }"
        ".section-card { background-color: #F8FAFC; border: 1px solid #E2E8F0; border-radius: 6px; padding: 12px; margin-bottom: 12px; }"
        "table { width: 100%; border-collapse: collapse; margin-bottom: 10px; }"
        "th { background-color: #F1F5F9; color: #475569; text-align: left; padding: 6px 8px; font-size: 11px; font-weight: 700; border-bottom: 2px solid #E2E8F0; }"
        "td { padding: 6px 8px; font-size: 11px; border-bottom: 1px solid #F1F5F9; color: #334155; }"
        "</style>";

    html += "<html><head>" + css + "</head><body>";

    if (type == "esp32" || type == "board") {
        html += "<h1>Microcontrolador ESP32-C3</h1>";
        html += "<span class=\"badge\">Cérebro do Sistema</span>";
        html += "<table>";
        html += "<tr><th>Evento Lógico</th><th>Descrição Técnica</th><th>Mapeamento Físico</th></tr>";
        html += "<tr><td><b>aoIniciar</b></td><td>Dispara na energização inicial da placa.</td><td>Equivalente ao <code>setup()</code> do Arduino.</td></tr>";
        html += "<tr><td><b>aoLoop</b></td><td>Dispara ciclicamente a cada tick da CPU.</td><td>Equivalente ao <code>loop()</code> do Arduino.</td></tr>";
        html += "</table>";

        code = 
            "// ========================================== \n"
            "// FUNÇÕES DE EVENTOS DO MICROCONTROLADOR ESP32\n"
            "// ========================================== \n\n"
            "// Função executada uma única vez na inicialização da placa (aoIniciar)\n"
            "void aoIniciar() {\n"
            "    // Insira sua inicialização física aqui\n"
            "}\n\n"
            "// Função executada repetidamente no loop principal cooperativo (aoLoop)\n"
            "void aoLoop() {\n"
            "    // Insira seu processamento cíclico aqui\n"
            "}\n";

    } else if (type == "button") {
        html += "<h1>Botão Pulsador (Pushbutton)</h1>";
        html += "<span class=\"badge\">Entrada Digital / Disparador Mecânico</span>";
        html += "<table>";
        html += "<tr><th>Evento Lógico</th><th>Gatilho de Hardware</th><th>Condição Elétrica</th></tr>";
        html += "<tr><td><b>aoPressionar</b></td><td>Borda de Descida (Falling Edge).</td><td>Transição instantânea de 3.3V para 0V.</td></tr>";
        html += "<tr><td><b>aoSoltar</b></td><td>Borda de Subida (Rising Edge).</td><td>Transição de 0V para 3.3V (Pull-up).</td></tr>";
        html += "<tr><td><b>aoClicar</b></td><td>Ciclo completo de pulso.</td><td>Detecção estável de descida seguida de subida.</td></tr>";
        html += "</table>";

        code = 
            "// ========================================== \n"
            "// FUNÇÕES DE EVENTOS DO BOTÃO PULSADOR (PULL-UP)\n"
            "// ========================================== \n\n"
            "// Função executada quando o botão é pressionado (aoPressionar)\n"
            "void aoPressionar() {\n"
            "    // Botão fechou contato com o terra (LOW)\n"
            "}\n\n"
            "// Função executada quando o botão é solto/liberado (aoSoltar)\n"
            "void aoSoltar() {\n"
            "    // Botão abriu contato e retornou para nível alto (HIGH)\n"
            "}\n\n"
            "// Função executada após o ciclo completo de clique (aoClicar)\n"
            "void aoClicar() {\n"
            "    // Ocorreu um pressionar estável seguido por soltar\n"
            "}\n";

    } else if (type == "led" || type == "rgb_led") {
        html += "<h1>Diodo Emissor de Luz (LED)</h1>";
        html += "<span class=\"badge\">Saída Digital e Analógica / Emissor Óptico</span>";
        html += "<table>";
        html += "<tr><th>Evento Lógico</th><th>Condição de Disparo</th><th>Fiação Típica</th></tr>";
        html += "<tr><td><b>aoLigar</b></td><td>Corrente física superior a 2.0mA circulando no anodo.</td><td>Pino Ânodo conectado ao GPIO e Cátodo ao GND.</td></tr>";
        html += "</table>";

        code = 
            "// ========================================== \n"
            "// FUNÇÕES DE EVENTOS DO DIODO EMISSOR DE LUZ (LED)\n"
            "// ========================================== \n\n"
            "// Função executada quando o LED liga de forma estável (aoLigar)\n"
            "void aoLigar() {\n"
            "    // Disparado quando corrente > 2mA é detectada no ânodo\n"
            "}\n";

    } else if (type == "potentiometer") {
        html += "<h1>Potenciômetro Rotativo</h1>";
        html += "<span class=\"badge\">Entrada Analógica / Divisor de Tensão</span>";
        html += "<table>";
        html += "<tr><th>Evento Lógico</th><th>Condição de Disparo</th><th>Faixa de Tensão</th></tr>";
        html += "<tr><td><b>aoGirar</b></td><td>Movimentação do cursor além do limite de histerese.</td><td>Sinal analógico de 0.0V a 3.3V.</td></tr>";
        html += "</table>";

        code = 
            "// ========================================== \n"
            "// FUNÇÕES DE EVENTOS DO POTENCIÔMETRO (Entrada Analógica)\n"
            "// ========================================== \n\n"
            "// Função executada continuamente quando o potenciômetro é girado (aoGirar)\n"
            "// O parâmetro 'valor' contém a leitura analógica quantizada no ADC (0 a 4095)\n"
            "void aoGirar(int valor) {\n"
            "    // Converte a leitura (0-4095) para tensão física aproximada (0-3.3V)\n"
            "}\n";

    } else if (type == "ldr") {
        html += "<h1>Sensor de Luz LDR</h1>";
        html += "<span class=\"badge\">Entrada Analógica / Sensor de Luminosidade</span>";
        html += "<table>";
        html += "<tr><th>Evento Lógico</th><th>Condição de Disparo</th><th>Faixa de Tensão</th></tr>";
        html += "<tr><td><b>aoAlterar</b></td><td>Mudança no nível de luminosidade além do limite de histerese.</td><td>Leitura analógica de 0 a 100% de luminosidade.</td></tr>";
        html += "</table>";

        code = 
            "// ========================================== \n"
            "// FUNÇÕES DE EVENTOS DO SENSOR LDR (Sensor de Luz)\n"
            "// ========================================== \n\n"
            "// Função executada quando a luminosidade do LDR é alterada (aoAlterar)\n"
            "// O parâmetro 'luminosidade' contém a leitura analógica (0 a 4095)\n"
            "void aoAlterar(int luminosidade) {\n"
            "    // Insira seu código de controle aqui\n"
            "}\n";

    } else if (type == "buzzer") {
        html += "<h1>Buzzer Piezoelétrico (Passivo)</h1>";
        html += "<span class=\"badge\">Saída de Frequência / Atuador Acústico</span>";
        html += "<table>";
        html += "<tr><th>Evento Lógico</th><th>Condição de Disparo</th><th>Fiação Típica</th></tr>";
        html += "<tr><td><b>aoTocar</b></td><td>Modulação activa de frequência no pino físico.</td><td>Terminal de Sinal conectado ao GPIO do ESP32.</td></tr>";
        html += "</table>";

        code = 
            "// ========================================== \n"
            "// FUNÇÕES DE EVENTOS DO BUZZER (Saída de Áudio)\n"
            "// ========================================== \n\n"
            "// Função executada quando o buzzer é ativado (aoTocar)\n"
            "void aoTocar() {\n"
            "    // Executa uma melodia ou tom no buzzer\n"
            "}\n";

    } else if (type == "motor") {
        html += "<h1>Servo Motor</h1>";
        html += "<span class=\"badge\">Saída PWM Especial / Posicionador Eletromecânico</span>";
        
        html += "<div class=\"section-card\">";
        html += "<h2>1. Comportamento Eletrofísico & Simulação</h2>";
        html += "<p>O Servo Motor modela um sistema eletromecânico preciso com controle em malha fechada:</p>";
        html += "<ul>";
        html += "<li><b>Modulação PWM por Pulso (50Hz):</b> O sinal opera em frequência fixa de 50Hz (período de 20ms). A largura do pulso ativo de tensão (t_on) comanda a posição física desejada:<br>";
        html += "- t_on = 1.0ms &rarr; 0&deg; de ângulo.<br>";
        html += "- t_on = 1.5ms &rarr; 90&deg; de ângulo.<br>";
        html += "- t_on = 2.0ms &rarr; 180&deg; de ângulo.</li>";
        html += "<li><b>Modelo Inercial:</b> O motor possui atraso de aceleração e atrito viscoso. O braço do motor no canvas se move com velocidade física finita baseada nas equações cinemáticas, imitando a inércia mecânica real do rotor.</li>";
        html += "</ul>";
        html += "</div>";

        html += "<h2>2. Eventos e Fiação do Componente</h2>";
        html += "<table>";
        html += "<tr><th>Evento Lógico</th><th>Condição de Disparo</th><th>Fiação Comum</th></tr>";
        html += "<tr><td><b>Rotação Visual</b></td><td>Variação na largura do pulso de controle no pino.</td><td>Sinal (GPIO), VCC (5V/3.3V), GND.</td></tr>";
        html += "</table>";

        code =
            "// ========================================== \n"
            "// FUNÇÕES DE CONTROLE DE SERVO MOTOR (PWM)\n"
            "// ========================================== \n\n"
            "// ── DRIVER NATIVO DE MOTOR ESP32 ──\n"
            "struct IdeMotorDriver {\n"
            "    int _pin = -1;\n"
            "    int _channel = 0;\n"
            "    int _type = 0; // 0=servo, 1=servo360, 2=dc\n"
            "#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3\n"
            "    void attach(int pin, int type = 0, int channel = -1) {\n"
            "        _pin = pin;\n"
            "        _type = type;\n"
            "        ledcAttach(_pin, _type == 2 ? 1000 : 50, 16);\n"
            "    }\n"
            "    void write(int val) {\n"
            "        if (_type == 2) { // DC PWM\n"
            "            int duty = map(val, 0, 100, 0, 65535);\n"
            "            ledcWrite(_pin, duty);\n"
            "        } else {\n"
            "            int pulse_us = map(val, 0, 180, 500, 2500);\n"
            "            int duty = (pulse_us * 65535) / 20000;\n"
            "            ledcWrite(_pin, duty);\n"
            "        }\n"
            "    }\n"
            "    void stop() {\n"
            "        write(_type == 1 ? 90 : 0);\n"
            "    }\n"
            "#else\n"
            "    void attach(int pin, int type = 0, int channel = 2) {\n"
            "        _pin = pin;\n"
            "        _type = type;\n"
            "        _channel = channel;\n"
            "        ledcSetup(_channel, _type == 2 ? 1000 : 50, 16);\n"
            "        ledcAttachPin(_pin, _channel);\n"
            "    }\n"
            "    void write(int val) {\n"
            "        if (_type == 2) { // DC PWM\n"
            "            int duty = map(val, 0, 100, 0, 65535);\n"
            "            ledcWrite(_channel, duty);\n"
            "        } else {\n"
            "            int pulse_us = map(val, 0, 180, 500, 2500);\n"
            "            int duty = (pulse_us * 65535) / 20000;\n"
            "            ledcWrite(_channel, duty);\n"
            "        }\n"
            "    }\n"
            "    void stop() {\n"
            "        write(_type == 1 ? 90 : 0);\n"
            "    }\n"
            "#endif\n"
            "};\n\n"
            "// Instância do driver para o motor\n"
            "IdeMotorDriver MOTOR_1;\n\n"
            "// Função executada para rotacionar o braço do motor (exemplo de uso)\n"
            "void moverServo(int angulo) {\n"
            "    // Envia o ângulo (0 a 180) para o driver do servo motor\n"
            "    MOTOR_1.write(angulo);\n"
            "}\n";

    } else if (auto* custom = dynamic_cast<CustomComponentItem*>(comp)) {
        CustomComponentDef def = custom->definition();
        html += QString("<h1>Componente Customizado: %1</h1>").arg(def.name);
        html += QString("<span class=\"badge\">Categoria: %1</span>").arg(def.category);
        
        html += "<div class=\"section-card\">";
        html += "<h2>1. Comportamento Eletrofísico & Simulação</h2>";
        html += QString("<p>Este componente foi criado de forma personalizada com as seguintes especificações eletrofísicas:</p>");
        html += "<ul>";
        html += QString("<li><b>Tensão Operacional:</b> %1</li>").arg(def.operatingVoltage);
        html += QString("<li><b>Consumo de Corrente:</b> %1 mA</li>").arg(def.currentConsumption);
        html += QString("<li><b>Protocolo/Leitura:</b> %1 (%2)</li>").arg(def.protocol).arg(def.readType);
        html += "</ul>";
        html += "</div>";

        html += "<h2>2. Pinos e Eventos do Componente</h2>";
        html += "<h3>Tabela de Fiação e Sinais</h3>";
        html += "<table>";
        html += "<tr><th>Nome do Pino</th><th>Direção</th><th>Tipo de Sinal</th><th>Função Avançada</th></tr>";
        for (const auto& pin : def.pins) {
            QString dir = pin.isOutput ? "Saída (OUT)" : "Entrada (IN)";
            html += QString("<tr><td><b>%1</b></td><td>%2</td><td>%3</td><td>%4</td></tr>")
                .arg(pin.name).arg(dir).arg(pin.signalType).arg(pin.role);
        }
        html += "</table>";

        if (!def.customEvents.isEmpty()) {
            html += "<h3>Eventos Lógicos Registrados</h3>";
            html += "<table>";
            html += "<tr><th>Evento</th><th>Função Callback</th><th>Mecanismo de Trigger</th></tr>";
            for (const auto& ev : def.customEvents) {
                html += QString("<tr><td><b>%1</b></td><td><code>%2</code></td><td>Debounce: %3ms, Gatilho: %4</td></tr>")
                    .arg(ev.name).arg(ev.callback).arg(ev.debounceMs).arg(ev.triggerMode);
            }
            html += "</table>";
        }

        code = 
            "// ========================================== \n"
            "// FUNÇÕES DE EVENTO DO COMPONENTE CUSTOMIZADO \n"
            "// ========================================== \n\n";

        if (!def.customEvents.isEmpty()) {
            for (const auto& ev : def.customEvents) {
                code += QString(
                    "// Evento: %1\n"
                    "// Gatilho: %2 (Debounce: %3ms)\n"
                    "void %4() {\n"
                    "    // Insira o código para tratar este evento aqui\n"
                    "}\n\n"
                ).arg(ev.name).arg(ev.triggerMode).arg(ev.debounceMs).arg(ev.callback);
            }
        } else {
            QString category = def.category;
            if (category == "digital_trigger") {
                code += 
                    "// Evento disparado no clique do componente\n"
                    "void aoClicar() {\n"
                    "    // Insira seu código aqui\n"
                    "}\n";
            } else if (category == "digital_actuator") {
                code += 
                    "// Evento disparado quando o atuador digital liga\n"
                    "void aoLigar() {\n"
                    "    // Insira seu código aqui\n"
                    "}\n";
            } else if (category == "analog_input") {
                code += 
                    "// Evento disparado quando a leitura analógica muda\n"
                    "void aoGirar(int valor) {\n"
                    "    // Insira seu código aqui\n"
                    "}\n";
            } else if (category == "active_actuator") {
                code += 
                    "// Evento disparado quando o atuador ativo é acionado\n"
                    "void aoTocar() {\n"
                    "    // Insira seu código aqui\n"
                    "}\n";
            } else {
                code += 
                    "// Este componente não possui eventos cadastrados.\n"
                    "// Defina eventos personalizados na criação do componente.\n";
            }
        }

    } else {
        // Componente genérico ou passivo (ex: resistor, capacitor)
        html += QString("<h1>Componente Genérico: %1</h1>").arg(comp->name());
        html += "<span class=\"badge\">Componente Passivo</span>";
        
        html += "<div class=\"section-card\">";
        html += "<h2>1. Comportamento Eletrofísico & Simulação</h2>";
        html += "<p>Este componente atua como elemento passivo resistivo ou reativo na malha elétrica da fiação:</p>";
        html += "<ul>";
        html += "<li><b>Associação de Impedâncias:</b> Modela a resistência à passagem de portadores de carga elétrica e quedas de tensão decorrentes no circuito virtual.</li>";
        html += "<li><b>Não-Programável:</b> Componentes passivos não executam blocos de código C++ diretamente, mas alteram a corrente e a tensão nos pinos dos microcontroladores conectados.</li>";
        html += "</ul>";
        html += "</div>";

        code = 
            "// ========================================== \n"
            "// DETALHES DO COMPONENTE PASSIVO \n"
            "// ========================================== \n\n"
            "// Componentes passivos ou genéricos não executam códigos C++ de evento.\n"
            "// Eles modificam fisicamente as tensões e correntes na simulação.\n";
    }

    // Compile actual event code from visual blocks or default template signatures if empty
    code = CodeGenerator::compileComponentEvents(comp, m_scene->components(), m_blockEditor->getEventBlockStorage());

    html += "</body></html>";
    browser->setHtml(html);
    mainLayout->addWidget(browser, 2); // stretch factor 2

    // Rótulo intermediário
    auto* codeLabel = new QLabel("Funções de Evento (C++ Editável):", &dialog);
    codeLabel->setStyleSheet("font-family: 'Segoe UI', sans-serif; font-size: 14px; font-weight: 600; color: #1E293B; margin-top: 8px;");
    mainLayout->addWidget(codeLabel);

    // Editor de código editável (Fundo claro)
    auto* codeEditor = new QPlainTextEdit(&dialog);
    codeEditor->setPlainText(code);
    codeEditor->setStyleSheet(
        "QPlainTextEdit { "
        "  background-color: #FFFFFF !important; "
        "  color: #0F172A !important; "
        "  border: 1px solid #CAD6E2 !important; "
        "  border-radius: 8px; "
        "  padding: 12px; "
        "  font-family: 'Consolas', 'Fira Code', 'Courier New', monospace; "
        "  font-size: 13px; "
        "  line-height: 1.4; "
        "}"
    );
    mainLayout->addWidget(codeEditor, 3); // stretch factor 3

    // Botões no rodapé
    auto* footer = new QHBoxLayout();
    
    auto* copyBtn = new QPushButton("Copiar Funções", &dialog);
    copyBtn->setStyleSheet(
        "QPushButton { background: #0EA5E9; border: none; border-radius: 6px; color: white; padding: 10px 20px; font-weight: 600; font-size: 13px; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QPushButton:hover { background: #0284C7; }"
        "QPushButton:pressed { background: #0369A1; }"
    );
    connect(copyBtn, &QPushButton::clicked, &dialog, [codeEditor]() {
        QGuiApplication::clipboard()->setText(codeEditor->toPlainText());
    });
    footer->addWidget(copyBtn);
    
    footer->addStretch();
    
    auto* closeBtn = new QPushButton("Fechar", &dialog);
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    footer->addWidget(closeBtn);
    
    mainLayout->addLayout(footer);

    dialog.exec();
}

void MainWindow::updatePlayActionState() {
    if (!m_playAction) return;
    
    if (m_lastBuildOk) {
        m_playAction->setIcon(QIcon(":/icons/play.svg"));
    } else {
        QPixmap disabledPix = QIcon(":/icons/play.svg").pixmap(32, 32, QIcon::Disabled, QIcon::Off);
        QIcon grayIcon(disabledPix);
        m_playAction->setIcon(grayIcon);
    }
}
#include <QInputDialog>

void MainWindow::openAiSettingsDialog() {
    QString projectDir = "";
    if (!m_currentProjectPath.isEmpty()) {
        projectDir = QFileInfo(m_currentProjectPath).path();
    }
    
    QString currentKey = m_aiOptimizer->getApiKey(projectDir);

    QDialog dialog(this);
    dialog.setWindowTitle("Configurar Copilot (GitHub Models)");
    dialog.setMinimumWidth(450);
    
    // Estética Frutiger Aero para o Dialog inteiro, corrigindo o bug do texto invisível
    dialog.setStyleSheet(
        "QDialog { "
        "   background: #FFFFFF; "
        "} "
        "QLabel { "
        "   color: #0F172A; "
        "   font-size: 13px; "
        "   font-family: 'Segoe UI', Arial; "
        "} "
        "QLineEdit { "
        "   background: #FFFFFF; "
        "   border: 1px solid #7FA2C9; "
        "   border-radius: 6px; "
        "   padding: 6px; "
        "   color: #0F172A; "
        "   selection-background-color: #3B82F6; "
        "} "
        "QLineEdit:focus { "
        "   border: 2px solid #3B82F6; "
        "} "
        "QPushButton { "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #E2F0FE, stop:0.46 #B8DCFF, stop:1 #8BBFFF); "
        "   border: 1px solid #7FA2C9; "
        "   border-radius: 6px; "
        "   padding: 6px 16px; "
        "   color: #0369A1; "
        "   font-weight: bold; "
        "} "
        "QPushButton:hover { "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.45 #F0F8FF, stop:0.46 #D9EDFF, stop:1 #B8DCFF); "
        "} "
        "QPushButton:pressed { "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8BBFFF, stop:1 #B8DCFF); "
        "} "
    );

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(15);
    layout->setContentsMargins(20, 20, 20, 20);
    
    auto* lblDesc = new QLabel("Insira seu Personal Access Token (PAT) do GitHub:\nEle será salvo com segurança em um arquivo '.api' na pasta do projeto.", &dialog);
    lblDesc->setWordWrap(true);
    layout->addWidget(lblDesc);

    auto* helpLayout = new QHBoxLayout();
    auto* lblHelp = new QLabel("<a href=\"https://github.com/settings/tokens\">Como gerar um Token (PAT)? Clique aqui!</a>", &dialog);
    lblHelp->setOpenExternalLinks(true);
    lblHelp->setStyleSheet("color: #0284C7; font-weight: bold; text-decoration: none;");
    helpLayout->addWidget(lblHelp);
    
    auto* btnQuestion = new QToolButton(&dialog);
    btnQuestion->setText("?");
    btnQuestion->setToolTip("Instruções detalhadas");
    btnQuestion->setStyleSheet(
        "QToolButton { "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.5 #E2F0FE, stop:0.51 #B8DCFF, stop:1 #8BBFFF); "
        "   border: 1px solid #7FA2C9; "
        "   border-radius: 12px; "
        "   color: #0369A1; "
        "   font-weight: bold; "
        "   width: 24px; "
        "   height: 24px; "
        "} "
        "QToolButton:hover { "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.5 #F0F8FF, stop:0.51 #D9EDFF, stop:1 #B8DCFF); "
        "} "
    );
    connect(btnQuestion, &QToolButton::clicked, this, [this]() {
        QMessageBox::information(this, "Como gerar o Token do GitHub",
            "1. Acesse sua conta do GitHub pelo navegador.\n"
            "2. Vá em Settings -> Developer settings -> Personal access tokens -> Tokens (classic).\n"
            "3. Clique em 'Generate new token (classic)'.\n"
            "4. Coloque qualquer nome, marque a caixinha 'repo' ou gere sem escopo mesmo (o Models API aceita).\n"
            "5. Clique em Generate, copie o código gerado e cole aqui!");
    });
    helpLayout->addWidget(btnQuestion);
    helpLayout->addStretch();
    layout->addLayout(helpLayout);

    auto* lineEdit = new QLineEdit(&dialog);
    lineEdit->setEchoMode(QLineEdit::Password);
    lineEdit->setText(currentKey);
    layout->addWidget(lineEdit);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(btnBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(btnBox);

    if (dialog.exec() == QDialog::Accepted) {
        QString text = lineEdit->text().trimmed();
        m_aiOptimizer->setApiKey(text, projectDir);
        logMessage("Chave do Copilot (GitHub) atualizada com sucesso.", "SYSTEM");
        
        // Garante que o arquivo .api seja ignorado no Git
        if (!projectDir.isEmpty()) {
            QFile gitignore(QDir(projectDir).filePath(".gitignore"));
            if (gitignore.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QString content = gitignore.readAll();
                if (!content.contains(".api")) {
                    QTextStream out(&gitignore);
                    if (!content.isEmpty() && !content.endsWith("\n")) out << "\n";
                    out << "\n# API Keys\n*.api\n";
                }
                gitignore.close();
            } else {
                if (gitignore.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&gitignore);
                    out << "# API Keys\n*.api\n";
                    gitignore.close();
                }
            }
        }
    }
}

void MainWindow::verifyCircuitWithAi() {
    if (m_scene->components().isEmpty()) {
        logMessage("O circuito está vazio. Adicione componentes antes de verificar.", "WARNING");
        return;
    }
    
    QString report = "RELATÓRIO DE CIRCUITO ELETRÔNICO:\n\nCOMPONENTES:\n";
    for (auto* comp : m_scene->components()) {
        report += "- " + comp->name() + " (Tipo: " + comp->componentType() + ")\n";
    }
    
    report += "\nCONEXÕES:\n";
    for (auto* cable : m_scene->cables()) {
        QString srcName = cable->sourceComponent() ? cable->sourceComponent()->name() : "Desconhecido";
        QString tgtName = cable->targetComponent() ? cable->targetComponent()->name() : "Desconhecido";
        report += "- " + srcName + " [" + cable->sourcePinName() + "] LIGADO EM " + tgtName + " [" + cable->targetPinName() + "]\n";
    }
    
    logMessage("Enviando circuito para avaliação da Inteligência Artificial... Aguarde.", "SYSTEM");
    
    auto conn1 = std::make_shared<QMetaObject::Connection>();
    auto conn2 = std::make_shared<QMetaObject::Connection>();
    
    *conn1 = connect(m_aiOptimizer, &AiOptimizer::optimizationFinished, this, [this, conn1, conn2](const QString& res) {
        logMessage("===============================\nRESULTADO DA AVALIAÇÃO (IA):\n" + res + "\n===============================", "SUCCESS");
        disconnect(*conn1); disconnect(*conn2);
    });
    
    *conn2 = connect(m_aiOptimizer, &AiOptimizer::optimizationError, this, [this, conn1, conn2](const QString& err) {
        logMessage("Erro na Avaliação da IA: " + err, "ERROR");
        disconnect(*conn1); disconnect(*conn2);
    });
    
    m_aiOptimizer->optimizeCode(report, AiOptimizer::VerifyCircuit);
}
