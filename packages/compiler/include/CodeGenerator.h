#pragma once
#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include "BlockEditor.h"

class ComponentItem;
class ConnectionCable;


class CodeGenerator {
public:
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

