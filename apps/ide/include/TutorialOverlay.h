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
#include <QTabBar>
#include <QTabWidget>
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
    QString dynamicTargetId; // e.g. "actionCmdCombo"
    bool hideNext = false;
};

class TutorialOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal pulseRadius READ pulseRadius WRITE setPulseRadius)

public:
    explicit TutorialOverlay(QWidget* parent = nullptr);
    ~TutorialOverlay() override;

    void setSteps(const QVector<TutorialStep>& steps);
    void start();
    void advance();
    int currentStep() const;

    qreal pulseRadius() const;
    void setPulseRadius(qreal r);
    
    void addVariableDragStep(int stepIndex, const QString& varKeyword, const QString& targetField = "target");
    void clearVariableDragSteps();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void showStep(int idx);
    void positionCard(const TutorialStep& step);
    void drawArrow(QPainter& p, const QRect& target);
    void updateMask();

    QWidget* resolveVariableWidget(const QString& keyword) const;
    QWidget* resolveTargetWidget(const TutorialStep& step) const;
    QRect getTargetRect(const TutorialStep& step) const;
    QWidget* resolveActionParamWidget() const;
    QWidget* resolveActionTargetWidget() const;
    QWidget* resolveActionCmdWidget() const;

    QVector<TutorialStep> m_steps;
    int m_currentStep = 0;
    qreal m_pulse = 0;

    QWidget* m_card = nullptr;
    QLabel* m_stepLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QWidget* m_hintBox = nullptr;
    QLabel* m_hintArrow = nullptr;
    QLabel* m_hintLabel = nullptr;
    QPushButton* m_btnSkip = nullptr;
    QPushButton* m_btnPrev = nullptr;
    QPushButton* m_btnNext = nullptr;
    QPropertyAnimation* m_pulseAnim = nullptr;
    QRegion m_lastMaskRegion;
    QMap<int, QString> m_dragStepKeywords;    // step index → variable keyword for drag animation
    QMap<int, QString> m_dragStepTargetFields; // step index → "target" or "param"
};
