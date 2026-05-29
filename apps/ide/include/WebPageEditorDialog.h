#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QJsonArray>

class QGraphicsScene;
class QGraphicsView;
class QCheckBox;

class WebPageEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit WebPageEditorDialog(QJsonObject& data, const QStringList& availableVars, QWidget* parent = nullptr);
    ~WebPageEditorDialog();

    void addElement(const QString& type, const QPointF& pos = QPointF(100, 100));
    void showQuickSearch(const QPointF& scenePos, const QPoint& globalPos);

    QString getEditEventCompId() const { return m_editEventCompId; }
    QString getEditEventName() const { return m_editEventName; }
    
    void requestEditEvent(const QString& compId, const QString& eventName);

    QStringList getAvailableVars() const { return m_availableVars; }

private slots:
    void saveAndClose();
    void handleDoubleClick(const QPoint& pos);
    void showContextMenu(const QPoint& pos);

private:
    void rebuildScene();
    
    QJsonObject& m_data; // Reference to MainWindow's data
    QJsonArray m_elements;
    
    QGraphicsScene* m_scene;
    QGraphicsView* m_view;
    QCheckBox* m_enableSwitch;
    
    QStringList m_availableVars;
    QString m_editEventCompId;
    QString m_editEventName;
};
