#ifndef DEEPSEEKCLIENT_H
#define DEEPSEEKCLIENT_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Thin OpenAI-compatible chat client aimed at DeepSeek.
 * API key / base URL / model live in QSettings (LiChenYang/LinuxHelper, group "ai").
 */
class DeepSeekClient : public QObject
{
    Q_OBJECT
public:
    explicit DeepSeekClient(QObject *parent = nullptr);

    bool isBusy() const { return m_busy; }

    static QString apiKey();
    static void setApiKey(const QString &key);
    static QString baseUrl();
    static void setBaseUrl(const QString &url);
    static QString model();
    static void setModel(const QString &model);

    /** Non-streaming chat completion. Emits chatFinished or chatFailed. */
    void chat(const QString &systemPrompt, const QString &userPrompt);

signals:
    void chatFinished(const QString &content);
    void chatFailed(const QString &error);

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_busy = false;
};

#endif // DEEPSEEKCLIENT_H
