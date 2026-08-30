#pragma once

#include <QObject>
#include <QVariantMap>
#include <QStringList>
#include <QMap>
#include <QDateTime>

class MetadataStore;
class AudioEngine;

/**
 * Agent 服务与工具注册表（v1.0 重写）
 *
 * 原则：
 *  - Agent 只能调用本类注册的工具，不得直连数据库/播放器内部对象
 *  - 写操作全部写入审计日志；高风险操作需 action.preview → 确认 → action.execute
 *  - 素材以「规范化绝对路径」为标识（文件夹直读模式，无稳定数字 id）
 */
namespace AgentPerm {
enum Permission {
    ReadLibrary = 0,
    ControlPlayer,
    EditMetadata,
    ManageLibrary,
    RunAnalysis
};
}

struct AgentTool {
    QString name;
    QString description;
    AgentPerm::Permission minPermission;
    QVariantMap paramsSchema;
    QString category;           // read / playback / write / preview
};

class AgentService : public QObject
{
    Q_OBJECT
public:
    AgentService(MetadataStore *db, AudioEngine *engine, QObject *parent = nullptr);

    QStringList toolNames() const;
    QVariantMap toolInfo(const QString &name) const;
    QVariantMap capabilities() const;
    QVariantMap status() const;

    QVariantMap call(const QString &tool, const QVariantMap &args, const QString &source);

    struct AuthScope {
        bool allowRead = true;
        bool allowPlayback = true;
        bool allowWrite = false;
        QString token;
    };
    void setAuthScope(const AuthScope &s) { scope_ = s; }
    AuthScope authScope() const { return scope_; }

    QString issuePreviewToken(const QString &tool, const QString &summary);
    bool consumePreviewToken(const QString &token, QString *tool, QString *summary);

signals:
    void eventNotify(const QString &event, const QVariantMap &payload);
    /// 请求主界面浏览某个文件夹（Agent 可让软件跳到指定目录）
    void browseRequested(const QString &path);

private:
    QVariantMap execRead(const QString &tool, const QVariantMap &args);
    QVariantMap execWrite(const QString &tool, const QVariantMap &args, const QString &source);
    void audit(const QString &tool, const QVariantMap &args, const QString &source,
               const QString &result);

    MetadataStore *db_ = nullptr;
    AudioEngine *engine_ = nullptr;
    AuthScope scope_;
    QMap<QString, AgentTool> tools_;
    struct PreviewToken { QString tool; QString summary; qint64 ts = 0; };
    QMap<QString, PreviewToken> previewTokens_;
};
