#include "deepseekclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

namespace {

constexpr char kOrg[] = "LiChenYang";
constexpr char kApp[] = "LinuxHelper";
constexpr char kDefaultBaseUrl[] = "https://api.deepseek.com/v1";
constexpr char kDefaultModel[] = "deepseek-chat";

} // namespace

DeepSeekClient::DeepSeekClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QString DeepSeekClient::apiKey()
{
    QSettings s(QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
    return s.value(QStringLiteral("ai/deepseekApiKey")).toString().trimmed();
}

void DeepSeekClient::setApiKey(const QString &key)
{
    QSettings s(QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
    s.setValue(QStringLiteral("ai/deepseekApiKey"), key.trimmed());
}

QString DeepSeekClient::baseUrl()
{
    QSettings s(QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
    const QString url = s.value(QStringLiteral("ai/baseUrl"),
                                QString::fromLatin1(kDefaultBaseUrl))
                            .toString()
                            .trimmed();
    return url.isEmpty() ? QString::fromLatin1(kDefaultBaseUrl) : url;
}

void DeepSeekClient::setBaseUrl(const QString &url)
{
    QSettings s(QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
    s.setValue(QStringLiteral("ai/baseUrl"), url.trimmed());
}

QString DeepSeekClient::model()
{
    QSettings s(QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
    const QString m = s.value(QStringLiteral("ai/model"),
                              QString::fromLatin1(kDefaultModel))
                          .toString()
                          .trimmed();
    return m.isEmpty() ? QString::fromLatin1(kDefaultModel) : m;
}

void DeepSeekClient::setModel(const QString &model)
{
    QSettings s(QString::fromLatin1(kOrg), QString::fromLatin1(kApp));
    s.setValue(QStringLiteral("ai/model"), model.trimmed());
}

void DeepSeekClient::chat(const QString &systemPrompt, const QString &userPrompt)
{
    if (m_busy) {
        emit chatFailed(QStringLiteral("已有请求进行中，请稍候。"));
        return;
    }

    const QString key = apiKey();
    if (key.isEmpty()) {
        emit chatFailed(QStringLiteral("未配置 DeepSeek API Key，请先在设置中填写。"));
        return;
    }

    QString endpoint = baseUrl();
    while (endpoint.endsWith(QLatin1Char('/')))
        endpoint.chop(1);
    if (!endpoint.endsWith(QStringLiteral("/chat/completions")))
        endpoint += QStringLiteral("/chat/completions");

    QJsonArray messages;
    if (!systemPrompt.trimmed().isEmpty()) {
        QJsonObject sys;
        sys.insert(QStringLiteral("role"), QStringLiteral("system"));
        sys.insert(QStringLiteral("content"), systemPrompt);
        messages.append(sys);
    }
    {
        QJsonObject user;
        user.insert(QStringLiteral("role"), QStringLiteral("user"));
        user.insert(QStringLiteral("content"), userPrompt);
        messages.append(user);
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), model());
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), false);
    body.insert(QStringLiteral("temperature"), 0.3);

    QNetworkRequest req{QUrl(endpoint)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", QByteArray("Bearer ") + key.toUtf8());
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    req.setTransferTimeout(60000);
#endif

    m_busy = true;
    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &DeepSeekClient::onReplyFinished);
}

void DeepSeekClient::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    m_busy = false;

    if (!reply) {
        emit chatFailed(QStringLiteral("网络回复为空。"));
        return;
    }
    reply->deleteLater();

    const QByteArray raw = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        QString detail = QString::fromUtf8(raw).trimmed();
        if (detail.size() > 400)
            detail = detail.left(400) + QStringLiteral("…");
        emit chatFailed(QStringLiteral("%1%2")
                            .arg(reply->errorString(),
                                 detail.isEmpty() ? QString()
                                                  : QStringLiteral(" | ") + detail));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject()) {
        emit chatFailed(QStringLiteral("响应不是合法 JSON。"));
        return;
    }

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("error"))) {
        const QJsonValue err = root.value(QStringLiteral("error"));
        QString msg;
        if (err.isObject())
            msg = err.toObject().value(QStringLiteral("message")).toString();
        else
            msg = err.toString();
        if (msg.isEmpty())
            msg = QStringLiteral("API 返回 error 字段");
        emit chatFailed(msg);
        return;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emit chatFailed(QStringLiteral("响应中没有 choices。"));
        return;
    }

    const QJsonObject message = choices.at(0).toObject().value(QStringLiteral("message")).toObject();
    QString content = message.value(QStringLiteral("content")).toString().trimmed();
    // Strip common markdown fences if model wraps the message
    if (content.startsWith(QStringLiteral("```"))) {
        const int firstNl = content.indexOf(QLatin1Char('\n'));
        if (firstNl > 0)
            content = content.mid(firstNl + 1);
        if (content.endsWith(QStringLiteral("```")))
            content.chop(3);
        content = content.trimmed();
    }

    if (content.isEmpty()) {
        emit chatFailed(QStringLiteral("模型返回空内容。"));
        return;
    }

    emit chatFinished(content);
}
