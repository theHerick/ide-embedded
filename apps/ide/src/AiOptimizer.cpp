#include "AiOptimizer.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

AiOptimizer::AiOptimizer(QObject* parent) : QObject(parent) {
    m_networkManager = new QNetworkAccessManager(this);
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &AiOptimizer::onReplyFinished);

    // Fallback inicial
    QSettings settings("Herick", "IDE-Embedded");
    m_apiKey = settings.value("ai_api_key", "").toString();
}

AiOptimizer::~AiOptimizer() {}

void AiOptimizer::setApiKey(const QString& key, const QString& projectPath) {
    m_apiKey = key;
    
    if (!projectPath.isEmpty()) {
        QFile file(QDir(projectPath).filePath(".api"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << key.trimmed();
            file.close();
            return;
        }
    }
    
    QSettings settings("Herick", "IDE-Embedded");
    settings.setValue("ai_api_key", key);
}

QString AiOptimizer::getApiKey(const QString& projectPath) const {
    if (!projectPath.isEmpty()) {
        QFile file(QDir(projectPath).filePath(".api"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = file.readAll().trimmed();
            file.close();
            if (!content.isEmpty()) return content;
        }
    }
    return m_apiKey;
}

void AiOptimizer::optimizeCode(const QString& originalCode, OptimizeMode mode) {
    if (m_apiKey.isEmpty()) {
        emit optimizationError("O Token do GitHub não está configurado. Configure no menu Ajustes.");
        return;
    }

    // Endpoint do GitHub Models (compatível com OpenAI API)
    QUrl url("https://models.inference.ai.azure.com/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());

    QString systemPrompt;
    switch(mode) {
        case OptimizePerformance:
            systemPrompt = "Atue como um Engenheiro de Software Embarcado Sênior. O código abaixo foi gerado automaticamente por blocos. Refatore e otimize este código C++ para Arduino visando máxima performance de CPU e legibilidade, mantendo EXATAMENTE a mesma lógica original. NÃO adicione dependências não-padrão. Retorne APENAS o código puro em C++, sem formatadores markdown (como ```cpp) e sem explicações.";
            break;
        case ReduceSize:
            systemPrompt = "Atue como um Engenheiro de Software Embarcado Sênior. Reduza o tamanho em bytes (Flash e RAM) do código C++ fornecido para Arduino o máximo possível sem alterar sua lógica original (ex: usar tipos menores, const PROGMEM onde aplicável, evitar strings literais duplicadas). Retorne APENAS o código puro em C++, sem formatadores markdown (como ```cpp) e sem explicações.";
            break;
        case TranslateToRust:
            systemPrompt = "Traduza o código C++ de Arduino fornecido para a linguagem Rust, utilizando as crates de emulação embarcada (esp-rs ou similiares). Mantenha a semântica original. Retorne APENAS o código puro em Rust, sem formatadores markdown (como ```rust) e sem explicações.";
            break;
        case TranslateToPython:
            systemPrompt = "Traduza o código C++ de Arduino fornecido para MicroPython (para rodar num ESP32 ou Raspberry Pi Pico). Mantenha a semântica. Retorne APENAS o código puro em MicroPython, sem formatadores markdown (como ```python) e sem explicações.";
            break;
    }

    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = systemPrompt;

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = "CÓDIGO ORIGINAL:\n" + originalCode;

    QJsonArray messages;
    messages.append(systemMsg);
    messages.append(userMsg);

    QJsonObject rootObj;
    rootObj["messages"] = messages;
    rootObj["model"] = "gpt-4o";
    rootObj["temperature"] = 0.2;

    QJsonDocument doc(rootObj);
    m_networkManager->post(request, doc.toJson());
}

void AiOptimizer::onReplyFinished(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        QString errText = reply->readAll();
        emit optimizationError("Erro de Rede do GitHub: " + reply->errorString() + "\n" + errText);
        reply->deleteLater();
        return;
    }

    QByteArray response = reply->readAll();
    QJsonDocument jsonDoc = QJsonDocument::fromJson(response);
    
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        emit optimizationError("Erro: Resposta JSON inválida do GitHub Models.");
        reply->deleteLater();
        return;
    }

    QJsonObject rootObj = jsonDoc.object();
    if (rootObj.contains("error")) {
        QJsonObject errObj = rootObj["error"].toObject();
        emit optimizationError("GitHub API Error: " + errObj["message"].toString());
        reply->deleteLater();
        return;
    }

    if (rootObj.contains("choices") && rootObj["choices"].isArray()) {
        QJsonArray choices = rootObj["choices"].toArray();
        if (!choices.isEmpty()) {
            QJsonObject firstChoice = choices[0].toObject();
            if (firstChoice.contains("message")) {
                QJsonObject msgObj = firstChoice["message"].toObject();
                QString resultText = msgObj["content"].toString();
                
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

    emit optimizationError("A API do GitHub retornou uma resposta inesperada ou vazia.");
    reply->deleteLater();
}
