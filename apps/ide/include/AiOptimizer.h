#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

class AiOptimizer : public QObject {
    Q_OBJECT
public:
    explicit AiOptimizer(QObject* parent = nullptr);
    ~AiOptimizer() override;

    void setApiKey(const QString& key, const QString& projectPath = "");
    QString getApiKey(const QString& projectPath = "") const;

    enum OptimizeMode {
        OptimizePerformance,
        ReduceSize,
        TranslateToRust,
        TranslateToPython,
        VerifyCircuit
    };

    void optimizeCode(const QString& originalCode, OptimizeMode mode);

signals:
    void optimizationFinished(const QString& optimizedCode);
    void optimizationError(const QString& errorMsg);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* m_networkManager;
    QString m_apiKey;
};
