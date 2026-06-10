#include "WorkspaceView.h"
#include "CustomComponent.h"
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLineEdit>
#include <QEvent>
#include <QCompleter>
#include <QAbstractItemView>
#include <QTimer>
#include <cmath>
#include <QSortFilterProxyModel>
#include <QApplication>
#include <QStringListModel>
#include <QHash>

namespace {
class AccentInsensitiveCompleterProxyModel : public QSortFilterProxyModel {
public:
    explicit AccentInsensitiveCompleterProxyModel(QLineEdit* lineEdit, QObject* parent = nullptr)
        : QSortFilterProxyModel(parent), m_lineEdit(lineEdit) {}

    void invalidate() {
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override {
        if (!m_lineEdit) return true;
        QString pattern = m_lineEdit->text().trimmed().toLower();
        pattern = removeAccents(pattern);

        QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
        QString data = sourceModel()->data(index, Qt::DisplayRole).toString().toLower();
        data = removeAccents(data);

        return data.contains(pattern);
    }

private:
    QLineEdit* m_lineEdit;

    static QString removeAccents(QString str) {
        static const QHash<QChar, QString> map = {
            {QChar(0x00E1), "a"}, {QChar(0x00E0), "a"}, {QChar(0x00E2), "a"}, {QChar(0x00E3), "a"}, {QChar(0x00E4), "a"},
            {QChar(0x00E9), "e"}, {QChar(0x00E8), "e"}, {QChar(0x00EA), "e"}, {QChar(0x00EB), "e"},
            {QChar(0x00ED), "i"}, {QChar(0x00EC), "i"}, {QChar(0x00EE), "i"}, {QChar(0x00EF), "i"},
            {QChar(0x00F3), "o"}, {QChar(0x00F2), "o"}, {QChar(0x00F4), "o"}, {QChar(0x00F5), "o"}, {QChar(0x00F6), "o"},
            {QChar(0x00FA), "u"}, {QChar(0x00F9), "u"}, {QChar(0x00FB), "u"}, {QChar(0x00FC), "u"},
            {QChar(0x00E7), "c"},
            {QChar(0x00C1), "a"}, {QChar(0x00C0), "a"}, {QChar(0x00C2), "a"}, {QChar(0x00C3), "a"}, {QChar(0x00C4), "a"},
            {QChar(0x00C9), "e"}, {QChar(0x00C8), "e"}, {QChar(0x00CA), "e"}, {QChar(0x00CB), "e"},
            {QChar(0x00CD), "i"}, {QChar(0x00CC), "i"}, {QChar(0x00CE), "i"}, {QChar(0x00CF), "i"},
            {QChar(0x00D3), "o"}, {QChar(0x00D2), "o"}, {QChar(0x00D4), "o"}, {QChar(0x00D5), "o"}, {QChar(0x00D6), "o"},
            {QChar(0x00DA), "u"}, {QChar(0x00D9), "u"}, {QChar(0x00DB), "u"}, {QChar(0x00DC), "u"},
            {QChar(0x00C7), "c"}
        };
        QString result;
        for (QChar c : str) {
            if (map.contains(c)) result += map[c];
            else result += c;
        }
        return result;
    }
};
} // namespace


WorkspaceView::WorkspaceView(QWidget* parent) : QGraphicsView(parent) {
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setRenderHint(QPainter::TextAntialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setDragMode(QGraphicsView::RubberBandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet("border: none; background: #0A0F1E;");
}

WorkspaceView::WorkspaceView(WorkspaceScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setRenderHint(QPainter::TextAntialiasing);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setDragMode(QGraphicsView::RubberBandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setStyleSheet("border: none; background: #0A0F1E;");
}

void WorkspaceView::wheelEvent(QWheelEvent* event) {
    // Zoom in/out with standard wheel rotation
    double factor = std::pow(1.15, event->angleDelta().y() / 120.0);
    
    // Guard zoom limits
    double currentScale = transform().m11();
    if ((factor < 1.0 && currentScale < 0.2) || (factor > 1.0 && currentScale > 5.0)) {
        return;
    }

    // Zoom anchored to the mouse position
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    scale(factor, factor);
    event->accept();
}

void WorkspaceView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // MATLAB Simulink style: if canvas has focus and they start typing, spawn search box
    QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint() && event->modifiers() == Qt::NoModifier) {
        auto* ws = dynamic_cast<WorkspaceScene*>(scene());
        if (ws && ws->isSimulating()) {
            event->ignore();
            return;
        }
        
        QPoint viewPos = mapFromGlobal(QCursor::pos());
        if (!viewport()->rect().contains(viewPos)) {
            viewPos = viewport()->rect().center();
        }
        spawnSearchBox(viewPos, text);
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void WorkspaceView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        setDragMode(QGraphicsView::RubberBandDrag);
        unsetCursor();
        event->accept();
    } else {
        QGraphicsView::keyReleaseEvent(event);
    }
}

void WorkspaceView::mousePressEvent(QMouseEvent* event) {
    setFocus();
    QGraphicsView::mousePressEvent(event);
}

void WorkspaceView::mouseDoubleClickEvent(QMouseEvent* event) {
    auto* ws = dynamic_cast<WorkspaceScene*>(scene());
    if (ws && ws->isSimulating()) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }
    
    QPoint viewPos = event->pos();
    QGraphicsItem* item = itemAt(viewPos);
    if (!item) {
        spawnSearchBox(viewPos);
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void WorkspaceView::spawnSearchBox(const QPoint& viewPos, const QString& initialText) {
    if (viewport()->findChild<QLineEdit*>("floatingSearchBox")) {
        return;
    }

    QLineEdit* searchEdit = new QLineEdit(viewport());
    searchEdit->setObjectName("floatingSearchBox");
    searchEdit->setPlaceholderText("Adicionar componente...");
    searchEdit->setStyleSheet(
        "QLineEdit#floatingSearchBox { "
        "  background-color: #FFFFFF !important; "
        "  border: 1.5px solid #CBD5E1; "
        "  border-radius: 8px; "
        "  color: #0F172A !important; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "  padding: 6px 12px; "
        "  min-width: 180px; "
        "}"
    );

    if (!initialText.isEmpty()) {
        searchEdit->setText(initialText);
    }

    searchEdit->adjustSize();
    int w = searchEdit->width();
    int h = searchEdit->height();

    // Bound coordinates to viewport boundaries
    int x = qMax(10, qMin(viewPos.x() - w/2, viewport()->width() - w - 10));
    int y = qMax(10, qMin(viewPos.y() - h/2, viewport()->height() - h - 10));

    searchEdit->setGeometry(x, y, w, h);
    searchEdit->show();
    searchEdit->raise();
    searchEdit->setFocus(Qt::OtherFocusReason);
    searchEdit->installEventFilter(this);

    // Setup premium, highly responsive autocomplete list
    QStringList componentsList;
    componentsList << "LED" << "LED RGB" << "Botão" << "Resistor" << "Capacitor" << "Potenciômetro" << "Sensor LDR (Luz)" << "Buzzer 5V" << "Motor Genérico" << "Terra (GND)"
                   << "Sensor Temperatura/Umidade DHT22"
                   << "Sensor Ultrassônico HC-SR04"
                   << "Lâmpada com Bocal";

    // Add registered custom modeled components
    for (const auto& def : CustomComponentManager::instance().registeredComponents()) {
        componentsList << def.name;
    }

    // Add components
    componentsList << "Módulo Relé";

    // Add "+ Modelar" option to search
    componentsList << "+ Modelar (Criar Novo Componente)";

    // Remove duplicates safely
    componentsList.removeDuplicates();

    // Create models for accent-insensitive search completer
    QStringListModel* sourceModel = new QStringListModel(componentsList, searchEdit);
    AccentInsensitiveCompleterProxyModel* proxyModel = new AccentInsensitiveCompleterProxyModel(searchEdit, searchEdit);
    proxyModel->setSourceModel(sourceModel);

    QCompleter* completer = new QCompleter(searchEdit);
    completer->setModel(proxyModel);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);

    connect(searchEdit, &QLineEdit::textChanged, proxyModel, &AccentInsensitiveCompleterProxyModel::invalidate);

    completer->popup()->setStyleSheet(
        "QListView { "
        "  background-color: #FFFFFF !important; "
        "  border: 1.5px solid #CBD5E1; "
        "  border-radius: 8px; "
        "  color: #0F172A !important; "
        "  padding: 4px; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 12px; "
        "  font-weight: 500; "
        "}"
        "QListView::item { "
        "  background-color: #FFFFFF !important; "
        "  color: #0F172A !important; "
        "  padding: 6px 12px; "
        "  border-radius: 4px; "
        "}"
        "QListView::item:hover { "
        "  background-color: #EEF2FF !important; "
        "  color: #1D4ED8 !important; "
        "}"
        "QListView::item:selected { "
        "  background-color: #2563EB !important; "
        "  color: #FFFFFF !important; "
        "}"
    );
    searchEdit->setCompleter(completer);

    auto adjustPopupSize = [completer]() {
        if (!completer) return;
        QAbstractItemView* popup = completer->popup();
        if (!popup) return;
        int count = completer->completionCount();
        if (count == 0) return;
        int maxVisible = completer->maxVisibleItems();
        int visibleCount = qMin(count, maxVisible);
        int itemHeight = 28;
        int totalHeight = visibleCount * itemHeight;
        int frameHeight = popup->frameWidth() * 2;
        int padding = 10;
        popup->setFixedHeight(totalHeight + frameHeight + padding);
    };

    connect(searchEdit, &QLineEdit::textChanged, searchEdit, [adjustPopupSize]() {
        QTimer::singleShot(10, adjustPopupSize);
    });

    // Force showing the list immediately so the user knows exactly what they can choose from!
    QTimer::singleShot(50, searchEdit, [completer, adjustPopupSize]() {
        completer->complete();
        adjustPopupSize();
    });

    auto handleAddition = [this, searchEdit, viewPos](const QString& textVal) {
        if (!searchEdit || searchEdit->property("processed").toBool()) return;
        searchEdit->setProperty("processed", true);

        QString text = textVal.trimmed();
        QPointF scenePos = mapToScene(viewPos);

        // Check if selected or typed "+ Modelar" / "modelar"
        if (text.contains("modelar", Qt::CaseInsensitive) || text.contains("criar novo", Qt::CaseInsensitive)) {
            emit requestComponentCreation();
            searchEdit->deleteLater();
            return;
        }

        QString textLower = text.toLower();
        QString type = "";
        QString name = "";

        if (textLower.contains("relé") || textLower.contains("rele") || textLower.contains("relay")) {
            WorkspaceScene* wScene = qobject_cast<WorkspaceScene*>(scene());
            if (wScene) wScene->addComponent("relay", "Módulo Relé", scenePos);
        } else if (textLower.contains("lampada") || textLower.contains("lâmpada") || textLower.contains("lamp")) {
            type = "lamp";
            name = "Lâmpada com Bocal";
        } else if (textLower.contains("bot") || textLower.contains("pulsador") || textLower.contains("btn") || textLower.contains("button") || textLower.contains("click")) {
            type = "button";
            name = "Botão Gatilho";
        } else if (textLower.contains("resistor")) {
            type = "resistor";
            name = "Resistor";
        } else if (textLower.contains("capacitor") || textLower.contains("capacitância")) {
            type = "capacitor";
            name = "Capacitor";
        } else if (textLower.contains("rgb") || textLower.contains("led rgb")) {
            type = "rgb_led";
            name = "LED RGB";
        } else if (textLower.contains("ldr") || textLower.contains("luminosidade")) {
            type = "ldr";
            name = "Sensor LDR (Luz)";
        } else if (textLower.contains("led") || textLower.contains("emissor") || textLower.contains("luz")) {
            type = "led";
            name = "LED Status";
        } else if (textLower.contains("terra") || textLower.contains("gnd") || textLower.contains("ground")) {
            type = "gnd";
            name = "Terra (GND)";
        } else if (textLower.contains("pot") || textLower.contains("var") || textLower.contains("knob")) {
            type = "potentiometer";
            name = "Potenciômetro";
        } else if (textLower.contains("buz") || textLower.contains("sound") || textLower.contains("som") || textLower.contains("apit")) {
            type = "buzzer";
            name = "Buzzer 5V";
        } else if (textLower.contains("motor") || textLower.contains("servo") || textLower.contains("passo") || textLower.contains("stepper") || textLower.contains("dc")) {
            type = "motor";
            name = "Motor Genérico";
        } else if (textLower.contains("hc") || textLower.contains("sr04") || textLower.contains("distancia") || textLower.contains("distância") || textLower.contains("ultrassonico") || textLower.contains("ultrassônico")) {
            type = "hcsr04";
            name = "Sensor Ultrassônico HC-SR04";
        } else if (textLower.contains("dht") || textLower.contains("temperatura") || textLower.contains("umidade") || textLower.contains("humidade") || textLower.contains("sensor")) {
            type = "dht22";
            name = "Sensor DHT22";
        } else {
            // Check if it matches any registered custom component name
            for (const auto& def : CustomComponentManager::instance().registeredComponents()) {
                if (def.name.trimmed().compare(text.trimmed(), Qt::CaseInsensitive) == 0 ||
                    def.name.toLower().contains(textLower)) {
                    type = def.type;
                    name = def.name;
                    break;
                }
            }
        }

        if (!type.isEmpty()) {
            WorkspaceScene* wScene = qobject_cast<WorkspaceScene*>(scene());
            if (wScene) {
                wScene->addComponent(type, name, scenePos);
            }
        }
        searchEdit->deleteLater();
    };

    connect(searchEdit, &QLineEdit::returnPressed, this, [searchEdit, completer, handleAddition]() {
        QString text = searchEdit->text().trimmed();
        if (completer && completer->completionCount() > 0) {
            QString bestMatch = completer->currentCompletion();
            if (!bestMatch.isEmpty()) {
                handleAddition(bestMatch);
                return;
            }
        }
        handleAddition(text);
    });

    connect(completer, QOverload<const QString&>::of(&QCompleter::activated), this, [handleAddition](const QString& textVal) {
        handleAddition(textVal);
    });

    connect(completer->popup(), &QAbstractItemView::clicked, this, [completer, handleAddition](const QModelIndex& index) {
        QString textVal = completer->popup()->model()->data(index, Qt::DisplayRole).toString();
        handleAddition(textVal);
    });
}

bool WorkspaceView::eventFilter(QObject* watched, QEvent* event) {
    if (watched->objectName() == "floatingSearchBox") {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                watched->deleteLater();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
            if (edit) {
                QCompleter* completer = edit->completer();
                if (completer && completer->popup() && completer->popup()->isVisible()) {
                    if (completer->popup()->geometry().contains(QCursor::pos())) {
                        return true; // Intercept focus out to prevent line edit from hiding popup on click
                    }
                }
            }
            // Delay deletion slightly so that mouse click selection on popup completes first
            QTimer::singleShot(150, watched, &QObject::deleteLater);
            return false;
        }
    }
    return QGraphicsView::eventFilter(watched, event);
}
