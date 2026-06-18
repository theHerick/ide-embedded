#pragma once
#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include "BlockEditor.h"

class ComponentItem;
class ConnectionCable;


class CodeGenerator {
private:
    static bool s_multitaskingEnabled;

public:
    static void setMultitaskingEnabled(bool enabled) { s_multitaskingEnabled = enabled; }
    static bool isMultitaskingEnabled() { return s_multitaskingEnabled; }

    static QString generateArduinoCode(
        const QVector<ComponentItem*>& components,
        const QVector<ConnectionCable*>& cables,
        const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage,
        const QJsonObject& webPageData = QJsonObject()
    );

    static QString compileComponentEvents(
        ComponentItem* comp,
        const QVector<ComponentItem*>& components,
        const QMap<QString, QVector<EventLogicBlock>>& eventBlockStorage
    );
};

