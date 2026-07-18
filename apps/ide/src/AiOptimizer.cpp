#include "AiOptimizer.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QSettings>
#include <QRegularExpression>

AiOptimizer::AiOptimizer(QObject* parent) : QObject(parent) {
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AiOptimizer::onReplyFinished);

    QSettings settings("Herick", "IDE-Embedded");
    m_apiKey = settings.value("ai_api_key", "").toString();
}

AiOptimizer::~AiOptimizer() {}

void AiOptimizer::setApiKey(const QString& key) {
    m_apiKey = key;
    QSettings settings("Herick", "IDE-Embedded");
    settings.setValue("ai_api_key", key);
}

QString AiOptimizer::getApiKey() const {
    return m_apiKey;
}

void AiOptimizer::optimizeCode(const QString& originalCode, OptimizeMode mode) {
    if (m_apiKey.isEmpty()) {
        emit optimizationError("A chave de API (API Key) não está configurada. Configure no menu Ajustes.");
        return;
    }

    QUrl url("https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=" + m_apiKey);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QString promptStr;
    switch(mode) {
        case OptimizePerformance:
            promptStr = "Atue como um Engenheiro de Software Embarcado Sênior. O código abaixo foi gerado automaticamente por blocos. Refatore e otimize este código C++ para Arduino visando máxima performance de CPU e legibilidade, mantendo EXATAMENTE a mesma lógica original. NÃO adicione dependências não-padrão. Retorne APENAS o código puro em C++, sem formatadores markdown (como ```cpp) e sem explicações.";
            break;
        case ReduceSize:
            promptStr = "Atue como um Engenheiro de Software Embarcado Sênior. Reduza o tamanho em bytes (Flash e RAM) do código C++ fornecido para Arduino o máximo possível sem alterar sua lógica original (ex: usar tipos menores, const PROGMEM onde aplicável, evitar strings literais duplicadas). Retorne APENAS o código puro em C++, sem formatadores markdown (como ```cpp) e sem explicações.";
            break;
        case TranslateToRust:
            promptStr = "Traduza o código C++ de Arduino fornecido para a linguagem Rust, utilizando as crates de emulação embarcada (esp-rs ou similiares). Mantenha a semântica original. Retorne APENAS o código puro em Rust, sem formatadores markdown (como ```rust) e sem explicações.";
            break;
        case TranslateToPython:
            promptStr = "Traduza o código C++ de Arduino fornecido para MicroPython (para rodar num ESP32 ou Raspberry Pi Pico). Mantenha a semântica. Retorne APENAS o código puro em MicroPython, sem formatadores markdown (como ```python) e sem explicações.";
            break;
    }

    QJsonObject messageObj;
    QJsonObject partsObj;
    partsObj["text"] = promptStr + "\n\nCÓDIGO ORIGINAL:\n" + originalCode;
    
    QJsonArray partsArray;
    partsArray.append(partsObj);

    QJsonObject contentsObj;
    contentsObj["parts"] = partsArray;

    QJsonArray contentsArray;
    contentsArray.append(contentsObj);

    QJsonObject rootObj;
    rootObj["contents"] = contentsArray;

    QJsonDocument doc(rootObj);
    m_networkManager->post(request, doc.toJson());
}

void AiOptimizer::onReplyFinished(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        emit optimizationError("Erro de Rede: " + reply->errorString() + "\nVerifique sua conexão ou a chave da API Gemini.");
        reply->deleteLater();
        return;
    }

    QByteArray response = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
    
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        emit optimizationError("Erro: Resposta JSON inválida da API Gemini.");
        reply->deleteLater();
        return;
    }

    QJsonObject rootObj = jsonDoc.object();
    if (rootObj.contains("error")) {
        QJsonObject errObj = rootObj["error"].toObject();
        emit optimizationError("API Error: " + errObj["message"].toString());
        reply->deleteLater();
        return;
    }

    if (rootObj.contains("candidates") && rootObj["candidates"].isArray()) {
        QJsonArray candidates = rootObj["candidates"].toArray();
        if (!candidates.isEmpty()) {
            QJsonObject firstCandidate = candidates[0].toObject();
            if (firstCandidate.contains("content")) {
                QJsonObject contentObj = firstCandidate["content"].toObject();
                if (contentObj.contains("parts") && contentObj["parts"].isArray()) {
                    QJsonArray parts = contentObj["parts"].toArray();
                    if (!parts.isEmpty()) {
                        QString resultText = parts[0].toObject()["text"].toString();
                        
                        // Clean up markdown block if the AI ignored the instruction
                        resultText.replace(QRegularExpression("^```(cpp|c|rust|python)\\n", QRegularExpression::CaseInsensitiveOption), "");
                        if (resultText.startsWith("```\n")) resultText = resultText.mid(4);
                        if (resultText.endsWith("```\n")) resultText.chop(4);
                        else if (resultText.endsWith("```")) resultText.chop(3);
                        
                        emit optimizationFinished(resultText.trimmed());
                        reply->deleteLater();
                        return;
                    }
                }
            }
        }
    }

    emit optimizationError("A API do Gemini retornou uma resposta inesperada ou vazia.");
    reply->deleteLater();
}
