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
            systemPrompt = "Atue como um Tutor e Engenheiro de Software Embarcado. O código abaixo foi criado por um usuário através de blocos. Refatore e otimize este código C++ visando performance, mantendo EXATAMENTE a mesma lógica original. IMPORTANTE: Você NÃO tem permissão para editar a lógica do projeto nem dar ideias a mais do que o usuário está fazendo. Apenas aplique melhores práticas (tipos corretos, remoção de redundâncias) para estimular o aprendizado do usuário, que manterá o controle total. Retorne APENAS o código puro em C++ (sem explicações e sem blocos de markdown ```cpp).";
            break;
        case ReduceSize:
            systemPrompt = "Atue como um Tutor e Engenheiro Embarcado. Reduza o tamanho em bytes (Flash e RAM) do código C++ fornecido o máximo possível (ex: usar tipos menores, macro PROGMEM). IMPORTANTE: Você NÃO tem poder para editar a lógica nem sugerir funcionalidades a mais. Seu objetivo é apenas otimizar o que existe para estimular o aprendizado do usuário. Retorne APENAS o código puro em C++ (sem explicações e sem blocos de markdown ```cpp).";
            break;
        case TranslateToRust:
            systemPrompt = "Traduza o código C++ de Arduino fornecido para a linguagem Rust, utilizando as crates de emulação embarcada (esp-rs ou similiares). Mantenha a semântica original. Retorne APENAS o código puro em Rust, sem formatadores markdown (como ```rust) e sem explicações.";
            break;
        case TranslateToPython:
            systemPrompt = "Traduza o código C++ de Arduino fornecido para MicroPython (para rodar num ESP32 ou Raspberry Pi Pico). Mantenha a semântica. Retorne APENAS o código puro em MicroPython, sem formatadores markdown (como ```python) e sem explicações.";
            break;
        case VerifyCircuit:
            systemPrompt = "Você é um Tutor de Eletrônica focado em Linting Físico. Vou te enviar a descrição de um circuito desenhado por um aluno. Sua tarefa é analisar fisicamente o circuito e encontrar apenas erros estruturais graves (ex: LED sem resistor, motores sem driver, ligações invertidas de polaridade). IMPORTANTE: Você não tem o poder de editar o circuito nem mudar o projeto. Seu objetivo é avisar sobre erros elétricos (incluindo se o Anodo/Catodo de LEDs ou pinos R/G/B/GND de um LED RGB estão ligados de forma invertida, sabendo que Anodo ou R/G/B recebem o sinal positivo e Catodo ou GND devem ir para o terra) e fornecer uma pequena dica didática de como corrigir (ex: 'recomendado colocar um resistor de 200 ohms no LED', ou 'inverta as pernas pois o GND deve ir no terra') para guiar o aprendizado, mantendo o controle na mão do usuário. Seja direto. Se estiver correto, responda apenas: 'O circuito está seguro e correto.'";
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
