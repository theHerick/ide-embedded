#pragma once
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <QApplication>
#include <QPointer>
#include <QLineEdit>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include "VariableSystem.h"

struct TutorialStep {
    QString title;
    QString description;
    QString hint;           // Green hint box text
    QWidget* targetWidget;  // Widget to spotlight (nullptr = center of screen)
    QRect customTargetRect; // Alternative to targetWidget
    enum ArrowDir { None, Up, Down, Left, Right } arrowDir = None;
    bool showHighlight = true; // If false, no blue pulsing glow or border is drawn (only the cutout hole)
};

class TutorialOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pulseRadius READ pulseRadius WRITE setPulseRadius)

public:
    explicit TutorialOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setMouseTracking(true);

        // Card widget
        m_card = new QWidget(this);
        m_card->setFixedWidth(420);
        m_card->setStyleSheet(
            "QWidget#tutCard { "
            "  background: #FFFFFF; "
            "  border: 1px solid #CBD5E1; "
            "  border-radius: 12px; "
            "}"
        );
        m_card->setObjectName("tutCard");

        auto* shadow = new QGraphicsDropShadowEffect(m_card);
        shadow->setBlurRadius(40);
        shadow->setOffset(0, 8);
        shadow->setColor(QColor(0, 0, 0, 80));
        m_card->setGraphicsEffect(shadow);

        auto* cardLayout = new QVBoxLayout(m_card);
        cardLayout->setContentsMargins(24, 20, 24, 20);
        cardLayout->setSpacing(10);

        // Step indicator
        m_stepLabel = new QLabel(m_card);
        m_stepLabel->setStyleSheet("font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 1.5px; color: #94A3B8;");
        cardLayout->addWidget(m_stepLabel);

        // Title
        m_titleLabel = new QLabel(m_card);
        m_titleLabel->setWordWrap(true);
        m_titleLabel->setStyleSheet("font-size: 18px; font-weight: 900; color: #0F172A; font-family: 'Segoe UI', Arial, sans-serif;");
        cardLayout->addWidget(m_titleLabel);

        // Description
        m_descLabel = new QLabel(m_card);
        m_descLabel->setWordWrap(true);
        m_descLabel->setStyleSheet("font-size: 13px; color: #475569; line-height: 1.5; font-family: 'Segoe UI', Arial, sans-serif;");
        cardLayout->addWidget(m_descLabel);

        // Hint box
        m_hintBox = new QWidget(m_card);
        m_hintBox->setStyleSheet("background: #F0FDF4; border: 1px solid #86EFAC; border-radius: 8px;");
        auto* hintLay = new QHBoxLayout(m_hintBox);
        hintLay->setContentsMargins(12, 8, 12, 8);
        m_hintArrow = new QLabel(m_hintBox);
        m_hintArrow->setText("👆");
        m_hintArrow->setFixedWidth(24);
        m_hintArrow->setStyleSheet("font-size: 16px;");
        hintLay->addWidget(m_hintArrow);
        m_hintLabel = new QLabel(m_hintBox);
        m_hintLabel->setWordWrap(true);
        m_hintLabel->setStyleSheet("font-size: 12px; font-weight: 600; color: #166534;");
        hintLay->addWidget(m_hintLabel, 1);
        cardLayout->addWidget(m_hintBox);

        // Button row
        auto* btnRow = new QHBoxLayout();
        btnRow->setSpacing(10);

        m_btnSkip = new QPushButton("Pular Tutorial", m_card);
        m_btnSkip->setStyleSheet(
            "QPushButton { background: transparent; border: none; color: #475569; font-size: 12px; font-weight: bold; padding: 8px 12px; }"
            "QPushButton:hover { color: #0F172A; text-decoration: underline; }"
        );

        m_btnPrev = new QPushButton("Anterior", m_card);
        m_btnPrev->setFixedWidth(90);
        m_btnPrev->setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "    stop:0 #FFFFFF, stop:0.4 #F1F5F9, stop:0.5 #E2E8F0, stop:1 #CBD5E1); "
            "  border: 1px solid #94A3B8; "
            "  border-radius: 6px; "
            "  color: #334155; "
            "  padding: 8px; "
            "  font-weight: bold; "
            "  font-size: 11px; "
            "  font-family: 'Segoe UI', Arial, sans-serif; "
            "}"
            "QPushButton:hover { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "    stop:0 #FFFFFF, stop:0.3 #F8FAFC, stop:0.6 #E2E8F0, stop:1 #94A3B8); "
            "  border-color: #64748B; "
            "  color: #0F172A; "
            "}"
            "QPushButton:disabled { "
            "  color: #CBD5E1; "
            "  border-color: #F1F5F9; "
            "}"
        );

        m_btnNext = new QPushButton("Próximo", m_card);
        m_btnNext->setFixedWidth(110);
        m_btnNext->setStyleSheet(
            "QPushButton { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "    stop:0 #60A5FA, stop:0.4 #3B82F6, stop:0.5 #2563EB, stop:1 #1D4ED8); "
            "  border: 1.5px solid rgba(255, 255, 255, 0.7); "
            "  border-radius: 6px; "
            "  color: #FFFFFF; "
            "  padding: 8px; "
            "  font-weight: 800; "
            "  font-size: 11px; "
            "  font-family: 'Segoe UI', Arial, sans-serif; "
            "}"
            "QPushButton:hover { "
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "    stop:0 #93C5FD, stop:0.3 #60A5FA, stop:0.6 #3B82F6, stop:1 #1E40AF); "
            "  border-color: #FFFFFF; "
            "}"
        );

        btnRow->addWidget(m_btnSkip);
        btnRow->addStretch();
        btnRow->addWidget(m_btnPrev);
        btnRow->addWidget(m_btnNext);
        cardLayout->addLayout(btnRow);

        // Pulse animation
        m_pulseAnim = new QPropertyAnimation(this, "pulseRadius", this);
        m_pulseAnim->setDuration(1200);
        m_pulseAnim->setStartValue(0.0);
        m_pulseAnim->setEndValue(20.0);
        m_pulseAnim->setLoopCount(-1);

        // Connections
        connect(m_btnNext, &QPushButton::clicked, this, [this]() {
            if (m_currentStep < m_steps.size() - 1) {
                m_currentStep++;
                showStep(m_currentStep);
            } else {
                close();
            }
        });
        connect(m_btnPrev, &QPushButton::clicked, this, [this]() {
            if (m_currentStep > 0) {
                m_currentStep--;
                showStep(m_currentStep);
            }
        });
        connect(m_btnSkip, &QPushButton::clicked, this, [this]() {
            close();
        });
    }

    ~TutorialOverlay() override {}

    void setSteps(const QVector<TutorialStep>& steps) {
        m_steps = steps;
        m_currentStep = 0;
    }

    void start() {
        if (m_steps.isEmpty()) return;
        m_currentStep = 0;
        resize(parentWidget()->size());
        show();
        raise();
        showStep(0);
        m_pulseAnim->start();
    }

    void advance() {
        if (m_currentStep < m_steps.size() - 1) {
            m_currentStep++;
            showStep(m_currentStep);
        } else {
            close();
        }
    }

    int currentStep() const { return m_currentStep; }

    qreal pulseRadius() const { return m_pulse; }
    void setPulseRadius(qreal r) { m_pulse = r; update(); }
    // stepIndex → {varKeyword, targetField} where targetField is "target" or "param"
    void addVariableDragStep(int stepIndex, const QString& varKeyword, const QString& targetField = "target") {
        m_dragStepKeywords[stepIndex] = varKeyword;
        m_dragStepTargetFields[stepIndex] = targetField;
    }

    void clearVariableDragSteps() {
        m_dragStepKeywords.clear();
        m_dragStepTargetFields.clear();
    }


protected:
    void paintEvent(QPaintEvent*) override {
        updateMask();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPainterPath path;
        path.addRect(rect());

        if (m_dragStepKeywords.contains(m_currentStep)) {
            const QString& kw = m_dragStepKeywords[m_currentStep];
            const QString  tf = m_dragStepTargetFields.value(m_currentStep, "target");
            QWidget* varW    = resolveVariableWidget(kw);
            QWidget* targetW = (tf == "param") ? resolveActionParamWidget() : resolveActionTargetWidget();
            if (varW && targetW && varW->isVisible()) {
                QPoint topLeftVar = varW->mapTo(parentWidget(), QPoint(0, 0));
                QRect rectVar(topLeftVar, varW->size());
                QRect expandedVar = rectVar.adjusted(-12, -12, 12, 12);

                QPoint topLeftTgt = targetW->mapTo(parentWidget(), QPoint(0, 0));
                QRect rectTgt(topLeftTgt, targetW->size());
                QRect expandedTgt = rectTgt.adjusted(-12, -12, 12, 12);

                QPainterPath holePath1;
                holePath1.addRoundedRect(expandedVar, 12, 12);
                path = path.subtracted(holePath1);

                QPainterPath holePath2;
                holePath2.addRoundedRect(expandedTgt, 12, 12);
                path = path.subtracted(holePath2);

                // Draw dark overlay with BOTH cutouts
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(15, 23, 42, 160));
                p.drawPath(path);

                // Draw glow rings around both
                QPen glowPen(QColor(59, 130, 246, 120), 3 + m_pulse * 0.3);
                p.setPen(glowPen);
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(expandedVar.adjusted(-(int)m_pulse, -(int)m_pulse, (int)m_pulse, (int)m_pulse), 14, 14);
                p.drawRoundedRect(expandedTgt.adjusted(-(int)m_pulse, -(int)m_pulse, (int)m_pulse, (int)m_pulse), 14, 14);

                // Draw white borders around both
                QPen borderPen(QColor(255, 255, 255, 200), 2.5);
                p.setPen(borderPen);
                p.drawRoundedRect(expandedVar, 12, 12);
                p.drawRoundedRect(expandedTgt, 12, 12);

                // Draw dashed arrow pointing from variable to target slot
                QPoint startPt(expandedVar.right(), expandedVar.center().y());
                QPoint endPt(expandedTgt.left(), expandedTgt.center().y());

                QPen dashPen(QColor(59, 130, 246, 200), 2.5, Qt::DashLine);
                p.setPen(dashPen);
                p.drawLine(startPt, endPt);

                // Draw arrowhead
                double angle = std::atan2(endPt.y() - startPt.y(), endPt.x() - startPt.x());
                int arrowSize = 12;
                QPointF p1 = endPt - QPointF(arrowSize * std::cos(angle - 0.4), arrowSize * std::sin(angle - 0.4));
                QPointF p2 = endPt - QPointF(arrowSize * std::cos(angle + 0.4), arrowSize * std::sin(angle + 0.4));

                QPainterPath arrowHead;
                arrowHead.moveTo(endPt);
                arrowHead.lineTo(p1);
                arrowHead.lineTo(p2);
                arrowHead.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(59, 130, 246, 220));
                p.drawPath(arrowHead);

                // Draw arrow from instruction card to variable block
                drawArrow(p, rectVar);
                return;
            }
        }

        QRect expanded;
        QRect spotRect;
        bool hasSpot = false;

        if (m_currentStep < m_steps.size()) {
            spotRect = getTargetRect(m_steps[m_currentStep]);
            if (!spotRect.isNull()) {
                expanded = spotRect.adjusted(-12, -12, 12, 12);
                QPainterPath holePath;
                holePath.addRoundedRect(expanded, 12, 12);
                path = path.subtracted(holePath);
                hasSpot = true;
            }
        }

        // Draw dark overlay with the spotlight hole cutout
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 23, 42, 160));
        p.drawPath(path);

        if (hasSpot) {
            // Glow ring (pulse)
            QPen glowPen(QColor(59, 130, 246, 120), 3 + m_pulse * 0.3);
            p.setPen(glowPen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(expanded.adjusted(-(int)m_pulse, -(int)m_pulse, (int)m_pulse, (int)m_pulse), 14, 14);

            // White border around spotlight
            QPen borderPen(QColor(255, 255, 255, 200), 2.5);
            p.setPen(borderPen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(expanded, 12, 12);

            // Draw arrow from card to spotlight
            drawArrow(p, spotRect);
        }
    }

    void resizeEvent(QResizeEvent*) override {
        if (m_currentStep < m_steps.size()) {
            positionCard(m_steps[m_currentStep]);
            updateMask();
        }
    }

    void updateMask() {
        QRegion r(rect());
        if (m_currentStep < m_steps.size()) {
            if (m_dragStepKeywords.contains(m_currentStep)) {
                const QString& kw = m_dragStepKeywords[m_currentStep];
                const QString  tf = m_dragStepTargetFields.value(m_currentStep, "target");
                QWidget* varW    = resolveVariableWidget(kw);
                QWidget* targetW = (tf == "param") ? resolveActionParamWidget() : resolveActionTargetWidget();
                if (varW && targetW && varW->isVisible()) {
                    QPoint topLeftVar = varW->mapTo(parentWidget(), QPoint(0, 0));
                    QRect rectVar(topLeftVar, varW->size());
                    
                    QPoint topLeftTgt = targetW->mapTo(parentWidget(), QPoint(0, 0));
                    QRect rectTgt(topLeftTgt, targetW->size());
                    
                    r = r.subtracted(QRegion(rectVar));
                    r = r.subtracted(QRegion(rectTgt));
                }
            } else {
                QRect spotRect = getTargetRect(m_steps[m_currentStep]);
                if (!spotRect.isNull()) {
                    r = r.subtracted(QRegion(spotRect));
                }
            }
        }
        if (m_card) {
            r = r.united(QRegion(m_card->geometry()));
        }
        
        if (r != m_lastMaskRegion) {
            m_lastMaskRegion = r;
            setMask(r);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        event->accept();
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        event->accept();
    }

private:
    void showStep(int idx) {
        if (idx < 0 || idx >= m_steps.size()) return;
        const auto& step = m_steps[idx];

        m_stepLabel->setText(QString("PASSO %1 DE %2").arg(idx + 1).arg(m_steps.size()));
        m_titleLabel->setText(step.title);

        m_descLabel->hide();
        m_hintBox->hide();

        m_btnPrev->setEnabled(idx > 0);
        m_btnNext->setText(idx == m_steps.size() - 1 ? "Concluir!" : "Próximo");

        // Change next button color on last step
        if (idx == m_steps.size() - 1) {
            m_btnNext->setStyleSheet(
                "QPushButton { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                "    stop:0 #34D399, stop:0.4 #10B981, stop:0.5 #059669, stop:1 #047857); "
                "  border: 1.5px solid rgba(255, 255, 255, 0.7); "
                "  border-radius: 6px; "
                "  color: #FFFFFF; "
                "  padding: 8px; "
                "  font-weight: 800; "
                "  font-size: 11px; "
                "  font-family: 'Segoe UI', Arial, sans-serif; "
                "}"
                "QPushButton:hover { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                "    stop:0 #6EE7B7, stop:0.3 #34D399, stop:0.6 #10B981, stop:1 #065F46); "
                "  border-color: #FFFFFF; "
                "}"
            );
        } else {
            m_btnNext->setStyleSheet(
                "QPushButton { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                "    stop:0 #60A5FA, stop:0.4 #3B82F6, stop:0.5 #2563EB, stop:1 #1D4ED8); "
                "  border: 1.5px solid rgba(255, 255, 255, 0.75); "
                "  border-radius: 6px; "
                "  color: #FFFFFF; "
                "  padding: 8px; "
                "  font-weight: 800; "
                "  font-size: 11px; "
                "  font-family: 'Segoe UI', Arial, sans-serif; "
                "}"
                "QPushButton:hover { "
                "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                "    stop:0 #93C5FD, stop:0.3 #60A5FA, stop:0.6 #3B82F6, stop:1 #1E40AF); "
                "  border-color: #FFFFFF; "
                "}"
            );
        }

        m_card->adjustSize();
        positionCard(step);
        update();
    }

    QWidget* resolveVariableWidget(const QString& keyword) const {
        if (!parentWidget()) return nullptr;
        for (auto* item : parentWidget()->findChildren<VisualVariableItem*>()) {
            if (item->getDef().type == VarType::PIN &&
                item->getDef().name.contains(keyword, Qt::CaseInsensitive) &&
                item->isVisible()) {
                return item;
            }
        }
        // Fallback: look for any visible VisualVariableItem matching keyword (not just PIN type)
        for (auto* item : parentWidget()->findChildren<VisualVariableItem*>()) {
            if (item->getDef().name.contains(keyword, Qt::CaseInsensitive) &&
                item->isVisible()) {
                return item;
            }
        }
        return nullptr;
    }

    QWidget* resolveTargetWidget(const TutorialStep& step) const {
        // Legacy: drag steps now handled by resolveVariableWidget
        return step.targetWidget;
    }

    QRect getTargetRect(const TutorialStep& step) const {
        QWidget* targetW = resolveTargetWidget(step);
        if (targetW && targetW->isVisible()) {
            QPoint topLeft = targetW->mapTo(parentWidget(), QPoint(0, 0));
            return QRect(topLeft, targetW->size());
        }
        if (!step.customTargetRect.isNull()) {
            return step.customTargetRect;
        }
        return QRect();
    }

    void positionCard(const TutorialStep& step) {
        QRect target = getTargetRect(step);
        int cx, cy;

        if (target.isNull()) {
            // Center of screen
            cx = (width() - m_card->width()) / 2;
            cy = (height() - m_card->height()) / 2;
        } else {
            // Position card relative to target
            switch (step.arrowDir) {
                case TutorialStep::Up:
                    cx = target.center().x() - m_card->width() / 2;
                    cy = target.bottom() + 30;
                    break;
                case TutorialStep::Down:
                    cx = target.center().x() - m_card->width() / 2;
                    cy = target.top() - m_card->height() - 30;
                    break;
                case TutorialStep::Left:
                    cx = target.right() + 30;
                    cy = target.center().y() - m_card->height() / 2;
                    break;
                case TutorialStep::Right:
                    cx = target.left() - m_card->width() - 30;
                    cy = target.center().y() - m_card->height() / 2;
                    break;
                default:
                    cx = target.center().x() - m_card->width() / 2;
                    cy = target.bottom() + 30;
                    break;
            }
        }

        // Clamp to screen bounds
        cx = qBound(20, cx, width() - m_card->width() - 20);
        cy = qBound(20, cy, height() - m_card->height() - 20);

        m_card->move(cx, cy);
    }

    void drawArrow(QPainter& p, const QRect& target) {
        if (target.isNull() || m_currentStep >= m_steps.size()) return;

        QPoint cardCenter(m_card->x() + m_card->width() / 2, m_card->y() + m_card->height() / 2);
        QPoint targetCenter = target.center();

        // Determine arrow endpoints based on card position relative to target
        QPoint arrowStart, arrowEnd;
        const auto& step = m_steps[m_currentStep];

        switch (step.arrowDir) {
            case TutorialStep::Up:
                arrowStart = QPoint(m_card->x() + m_card->width() / 2, m_card->y());
                arrowEnd = QPoint(target.center().x(), target.bottom() + 14);
                break;
            case TutorialStep::Down:
                arrowStart = QPoint(m_card->x() + m_card->width() / 2, m_card->y() + m_card->height());
                arrowEnd = QPoint(target.center().x(), target.top() - 14);
                break;
            case TutorialStep::Left:
                arrowStart = QPoint(m_card->x(), cardCenter.y());
                arrowEnd = QPoint(target.right() + 14, target.center().y());
                break;
            case TutorialStep::Right:
                arrowStart = QPoint(m_card->x() + m_card->width(), cardCenter.y());
                arrowEnd = QPoint(target.left() - 14, target.center().y());
                break;
            default:
                return;
        }

        // Draw dashed line
        QPen dashPen(QColor(59, 130, 246, 200), 2.5, Qt::DashLine);
        p.setPen(dashPen);
        p.drawLine(arrowStart, arrowEnd);

        // Draw arrowhead
        double angle = std::atan2(arrowEnd.y() - arrowStart.y(), arrowEnd.x() - arrowStart.x());
        int arrowSize = 12;
        QPointF p1 = arrowEnd - QPointF(arrowSize * std::cos(angle - 0.4), arrowSize * std::sin(angle - 0.4));
        QPointF p2 = arrowEnd - QPointF(arrowSize * std::cos(angle + 0.4), arrowSize * std::sin(angle + 0.4));

        QPainterPath arrowHead;
        arrowHead.moveTo(arrowEnd);
        arrowHead.lineTo(p1);
        arrowHead.lineTo(p2);
        arrowHead.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(59, 130, 246, 220));
        p.drawPath(arrowHead);
    }

    QWidget* resolveActionParamWidget() const {
        if (parentWidget()) {
            for (auto* item : parentWidget()->findChildren<QWidget*>()) {
                if (item->objectName() == "actionParamEdit" && item->isVisible()) {
                    return item;
                }
            }
        }
        return nullptr;
    }

    QWidget* resolveActionTargetWidget() const {
        if (parentWidget()) {
            for (auto* item : parentWidget()->findChildren<QLineEdit*>()) {
                if (item->objectName() == "actionTargetEdit") {
                    return item;
                }
            }
            for (auto* item : parentWidget()->findChildren<QWidget*>()) {
                if (item->objectName() == "actionTargetEdit") {
                    return item;
                }
            }
        }
        return nullptr;
    }

    QVector<TutorialStep> m_steps;
    int m_currentStep = 0;
    qreal m_pulse = 0;

    QWidget* m_card;
    QLabel* m_stepLabel;
    QLabel* m_titleLabel;
    QLabel* m_descLabel;
    QWidget* m_hintBox;
    QLabel* m_hintArrow;
    QLabel* m_hintLabel;
    QPushButton* m_btnSkip;
    QPushButton* m_btnPrev;
    QPushButton* m_btnNext;
    QPropertyAnimation* m_pulseAnim;
    QRegion m_lastMaskRegion;
    QMap<int, QString> m_dragStepKeywords;    // step index → variable keyword for drag animation
    QMap<int, QString> m_dragStepTargetFields; // step index → "target" or "param"
};
